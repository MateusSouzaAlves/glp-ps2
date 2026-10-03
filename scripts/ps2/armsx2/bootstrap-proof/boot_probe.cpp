// SPDX-License-Identifier: GPL-3.0-or-later
// Private BIOS/game boot proof, using the shared persistent-session frontend.
#include "libretro_frontend.h"

namespace {
using namespace emuorbit::armsx2::frontend;
// Queued Java image observers never retain a pointer to the session.
std::atomic<bool> guestImageConfirmed{false};

// Every method runs on the JNI/frontend owner. offer() must synchronously COPY
// the borrowed direct view; no Java callback is made from the CPU/GS threads.
class AudioBridge final {
public:
    AudioBridge(JNIEnv* env, jobject target) : env_(env), target_(target) {
        if (!target_) return;
        jclass type = env_->GetObjectClass(target_);
        if (!type) throw std::runtime_error("No endpoint bridge class");
        offer_ = env_->GetMethodID(type, "offer", "(Ljava/nio/ByteBuffer;ID)V");
        pause_ = env_->GetMethodID(type, "pause", "()V");
        resume_ = env_->GetMethodID(type, "resume", "()V");
        stop_ = env_->GetMethodID(type, "stop", "()V");
        env_->DeleteLocalRef(type);
        check();
        if (!offer_ || !pause_ || !resume_ || !stop_) throw std::runtime_error("Endpoint bridge ABI mismatch");
    }
    void offer(int16_t* copied, size_t frames, double rate) {
        if (!target_ || !frames) return;
        jobject view = env_->NewDirectByteBuffer(copied, static_cast<jlong>(frames * 4));
        if (!view) { check(); throw std::runtime_error("No copied PCM view"); }
        env_->CallVoidMethod(target_, offer_, view, static_cast<jint>(frames), rate);
        env_->DeleteLocalRef(view); check();
    }
    void pause() { invoke(pause_); }
    void resume() { invoke(resume_); }
    void stop() { invoke(stop_); }
private:
    void invoke(jmethodID operation) {
        if (target_) { env_->CallVoidMethod(target_, operation); check(); }
    }
    void check() {
        if (env_->ExceptionCheck()) {
            env_->ExceptionDescribe(); env_->ExceptionClear();
            throw std::runtime_error("Android audio endpoint bridge failed (Java detail retained in log)");
        }
    }
    JNIEnv* env_;
    jobject target_;
    jmethodID offer_ = nullptr, pause_ = nullptr, resume_ = nullptr, stop_ = nullptr;
};

std::string execute(ANativeWindow* window, const std::string& root, int frames, const std::string& game,
        const std::string& resources, const std::string& system, const std::string& biosDirectory,
        const std::string& biosName, bool verifyLifecycle, const std::array<ANativeWindow*, 2>& replacements,
        AudioBridge& endpoint) {
    if (root.rfind("/data/local/tmp/emuorbit-armsx2-bootstrap-", 0) != 0
            || root.find("..") != std::string::npos || root.size() > 200 || frames < 30 || frames > 1800)
        throw std::runtime_error("Only a dedicated private bootstrap root is permitted");
    const bool layout = !system.empty();
    const std::string providedName = "ps2-bios-f4c948e61a291d4b3f92a141e550cf8357204287a31ff784caccbedaef910c9d.bin";
    if (layout && (system != root + "/session-store-proof/ps2-armsx2-data/games/e1de4720-57f9-45ae-b922-f993c5b0cddd/system"
            || biosDirectory != root + "/session-store-proof/ps2-armsx2-firmware" || biosName != providedName))
        throw std::runtime_error("Only production layout in a dedicated QA root is permitted");
    const std::string firmware = layout ? biosDirectory + "/" + biosName : root + "/system/pcsx2/bios/scph39001.bin";
    const std::string sidecar = firmware.substr(0, firmware.size() - 4);
    struct stat bios{};
    if (stat(firmware.c_str(), &bios) != 0
            || !S_ISREG(bios.st_mode) || bios.st_size != 4 * 1024 * 1024)
        throw std::runtime_error("Missing/invalid private test BIOS");
    Frontend proof;
    proof.system = layout ? system : root + "/system";
    proof.game = game;
    if (layout) {
        proof.biosBase = providedName.substr(0, providedName.size() - 4);
        proof.overrides["emuorbit_armsx2_bios_directory"] = biosDirectory;
        proof.overrides["armsx2_bios"] = biosName;
    }
    if (!resources.empty()) {
        if (resources.rfind("/data/local/tmp/emuorbit-armsx2-bootstrap-", 0) != 0
                || resources.find("..") != std::string::npos || resources.size() > 512)
            throw std::runtime_error("Only private verified QA resources are permitted");
        proof.overrides["emuorbit_armsx2_resources"] = resources;
    }
    if (!game.empty()) {
        struct stat media{};
        if (game.rfind("/sdcard/Download/EmuOrbit-PS2-tests/Extracted/", 0) != 0
                || game.find("..") != std::string::npos || game.size() > 256
                || game.size() < 4 || game.substr(game.size() - 4) != ".iso"
                || stat(game.c_str(), &media) != 0 || !S_ISREG(media.st_mode)
                || media.st_size < 1024 * 1024 || media.st_size > 12LL * 1024 * 1024 * 1024)
            throw std::runtime_error("Only an existing ISO from the authorized test corpus is permitted");
    }
    // NVM is written only after a guest write. MEC can already exist while
    // NVM is still legitimately absent on a second boot; classify separately.
    proof.nvmWasMissing = access((sidecar + ".nvm").c_str(), F_OK) != 0;
    proof.mecWasMissing = access((sidecar + ".mec").c_str(), F_OK) != 0;
    Frontend* expected = nullptr;
    if (!active.compare_exchange_strong(expected, &proof)) throw std::runtime_error("Probe session busy");
    guestImageConfirmed.store(false);
    void* library = nullptr;
    bool initialized = false, loaded = false, reset = false;
    void (*deinit)() = nullptr;
    void (*unload)() = nullptr;
    std::string result;
    try {
        library = dlopen((root + "/armsx2_libretro.so").c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) throw std::runtime_error(dlerror());
        auto hostVersion = symbol<EmuOrbitArmsHostVersion>(library, "retro_emuorbit_host_abi_version");
        auto setPaused = symbol<EmuOrbitArmsSetPaused>(library, "retro_emuorbit_set_paused_v1");
        auto snapshot = symbol<EmuOrbitArmsSnapshot>(library, "retro_emuorbit_snapshot_v1");
        if (hostVersion() != EMUORBIT_ARMS_HOST_ABI || setPaused(2) != EMUORBIT_ARMS_INVALID_ARGUMENT)
            throw std::runtime_error("Private lifecycle ABI mismatch");
        deinit = symbol<void(*)()>(library, "retro_deinit");
        unload = symbol<void(*)()>(library, "retro_unload_game");
        symbol<void(*)(retro_environment_t)>(library, "retro_set_environment")(environment);
        symbol<void(*)(retro_video_refresh_t)>(library, "retro_set_video_refresh")(video);
        symbol<void(*)(retro_audio_sample_t)>(library, "retro_set_audio_sample")(audio);
        symbol<void(*)(retro_audio_sample_batch_t)>(library, "retro_set_audio_sample_batch")(audioBatch);
        symbol<void(*)(retro_input_poll_t)>(library, "retro_set_input_poll")(pollInput);
        symbol<void(*)(retro_input_state_t)>(library, "retro_set_input_state")(input);
        symbol<void(*)()>(library, "retro_init")(); initialized = true;
        const retro_game_info media{game.c_str(), nullptr, 0, nullptr};
        loaded = symbol<bool(*)(const retro_game_info*)>(library, "retro_load_game")(game.empty() ? nullptr : &media);
        if (!loaded) throw std::runtime_error("Core rejected private boot");
        if (!proof.negotiation) throw std::runtime_error("No Vulkan negotiation; Null/GL fallback is not a pass");
        std::string error;
        proof.host = VulkanRenderHost::create(window, 960, 720,
                reinterpret_cast<const VulkanNegotiationInterface*>(proof.negotiation), error);
        if (!proof.host) throw std::runtime_error("Vulkan host: " + error);
        proof.hw.context_reset(); reset = true;
        auto run = symbol<void(*)()>(library, "retro_run");
        const auto start = std::chrono::steady_clock::now();
        std::array<int16_t, emuorbit::armsx2::PcmMailbox::kCapacity * 2> pcmOutput{};
        bool acquired = false;
        unsigned pauseCycles = 0, lastResumeFresh = 0;
        unsigned surfaceRebinds = 0;
        uint64_t stoppedCycles = 0;
        const int maximumCalls = game.empty() ? frames * 20 : 60000;
        for (int i = 0; (proof.fresh < static_cast<unsigned>(frames)
                || (!game.empty() && !guestImageConfirmed.load())
                || (verifyLifecycle && (pauseCycles < 3 || proof.fresh - lastResumeFresh < 10)))
                && i < maximumCalls && proof.errors.load() == 0; ++i) {
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(game.empty() ? 20 : 45)) break;
            // Acquire before set_image: beginCoreFrame clears the prior image.
            // A duplicate does not publish another image. Retain this acquired
            // frame until a fresh one arrives; don't replay old semaphores or
            // present a fabricated black startup frame as evidence of boot.
            if (!acquired && !proof.host->beginCoreFrame(error)) throw std::runtime_error(error);
            acquired = true;
            const auto before = proof.fresh;
            run();
            const size_t copiedFrames = proof.pcm.drain(pcmOutput.data(), emuorbit::armsx2::PcmMailbox::kCapacity);
            endpoint.offer(pcmOutput.data(), copiedFrames, proof.rate); // View expires on return; pipeline owns a copy.
            if (proof.fresh == before) { usleep(1000); continue; }
            if (!proof.host->presentCoreFrame(proof.width, proof.height, error)) throw std::runtime_error(error);
            acquired = false;
            if (verifyLifecycle && pauseCycles < 3 && proof.fresh >= static_cast<unsigned>(frames / 3)
                    && proof.fresh - lastResumeFresh >= 12) {
                int foreign = 0;
                std::thread other([&foreign, setPaused] { foreign = setPaused(1); });
                other.join();
                if (foreign != EMUORBIT_ARMS_WRONG_OWNER) throw std::runtime_error("Foreign lifecycle owner admitted");
                auto requireLifecycle = [](int status) {
                    if (status == EMUORBIT_ARMS_TIMEOUT || status == EMUORBIT_ARMS_FAILED) {
                        // Unknown CPU/GS work must NEVER be dlclosed/deinitialized.
                        // This disposable proof exits; the product will quarantine.
                        std::fputs("BOOTSTRAP_FAIL terminal lifecycle; process exit required, no native teardown attempted\n", stderr);
                        std::fflush(stderr); _exit(23);
                    }
                    if (status != EMUORBIT_ARMS_OK) throw std::runtime_error("Lifecycle request not ready/invalid");
                };
                requireLifecycle(setPaused(1));
                requireLifecycle(setPaused(1)); // Idempotent, still synchronized.
                proof.input.clear(); proof.pcm.discard();
                endpoint.pause();
                unsigned state = 0;
                uint64_t first = 0, second = 0;
                // Allow any final execution batch to return before measuring.
                usleep(30000);
                requireLifecycle(snapshot(&state, &first));
                if (state != EMUORBIT_ARMS_PAUSED) throw std::runtime_error("VM did not confirm paused state");
                usleep(150000);
                requireLifecycle(snapshot(&state, &second));
                if (state != EMUORBIT_ARMS_PAUSED || first != second)
                    throw std::runtime_error("Guest EE cycle advanced while paused");
                stoppedCycles = second;
                if (replacements[0] && pauseCycles < 2) {
                    bool foreignSurface = true;
                    std::thread otherSurface([&foreignSurface, &proof] {
                        std::string foreignError;
                        foreignSurface = proof.host->suspendPresentation(foreignError);
                    });
                    otherSurface.join();
                    if (foreignSurface || !proof.host->hasPresentation())
                        throw std::runtime_error("Foreign Surface owner admitted or mutated presentation");
                    auto requireSurface = [](bool confirmed, const std::string& detail) {
                        if (confirmed) return;
                        std::fprintf(stderr, "BOOTSTRAP_FAIL unconfirmed Surface operation: %s; process exit required\n", detail.c_str());
                        std::fflush(stderr); _exit(23); // No deinit/dlclose after an unknown GPU result.
                    };
                    auto* interface = proof.host->interface();
                    const auto device = interface->device;
                    requireSurface(proof.host->suspendPresentation(error), error);
                    if (proof.host->hasPresentation()) throw std::runtime_error("Surface not suspended");
                    if (proof.host->beginCoreFrame(error)) throw std::runtime_error("Frame admitted without Surface");
                    requireSurface(proof.host->suspendPresentation(error), error);
                    if (proof.host->replacePresentation(nullptr, 0, 0, error))
                        throw std::runtime_error("Invalid replacement Surface admitted");
                    const unsigned width = pauseCycles == 0 ? 720 : 960;
                    const unsigned height = pauseCycles == 0 ? 960 : 720;
                    error.clear();
                    requireSurface(proof.host->replacePresentation(replacements[pauseCycles], width, height, error), error);
                    if (proof.host->interface() != interface || interface->device != device
                            || proof.host->presentationWidth() != width || proof.host->presentationHeight() != height)
                        throw std::runtime_error("Surface replacement restarted device/context or changed expected extent");
                    uint64_t still = 0;
                    requireLifecycle(snapshot(&state, &still));
                    if (state != EMUORBIT_ARMS_PAUSED || still != stoppedCycles)
                        throw std::runtime_error("Surface replacement restarted/advanced the guest");
                    ++surfaceRebinds;
                    std::fprintf(stderr, "SURFACE_REBIND_PASS epoch=%u extent=%ux%u device_preserved=1 guest_preserved=1\n",
                            surfaceRebinds, width, height);
                }
                endpoint.resume();
                requireLifecycle(setPaused(0));
                requireLifecycle(setPaused(0)); // No unpaced snapshot of a running VM.
                ++pauseCycles; lastResumeFresh = proof.fresh;
                std::fprintf(stderr, "LIFECYCLE_CYCLE_PASS cycle=%u paused_ee_cycles=%llu stable_ms=150\n",
                        pauseCycles, static_cast<unsigned long long>(stoppedCycles));
            }
        }
        if (proof.fresh < static_cast<unsigned>(frames) || proof.softwareFrame
                || proof.errors.load() != 0 || proof.nvramDefaults.load() > 1
                || proof.mecDefaults.load() > 1 || proof.rate != 48000.0
                || (verifyLifecycle && (pauseCycles != 3 || proof.fresh - lastResumeFresh < 10))
                || (replacements[0] && surfaceRebinds != 2)
                || (!game.empty() && (!proof.gameIdentified.load() || !guestImageConfirmed.load())))
            throw std::runtime_error("Frame goal/core error/timing/identification check failed: fresh="
                    + std::to_string(proof.fresh) + " errors=" + std::to_string(proof.errors.load())
                    + " rate=" + std::to_string(proof.rate));
        if (verifyLifecycle) {
            const int paused = setPaused(1);
            if (paused != EMUORBIT_ARMS_OK) {
                std::fputs("BOOTSTRAP_FAIL final pause unconfirmed; process exit required\n", stderr);
                std::fflush(stderr); _exit(23);
            }
            endpoint.stop();
        }
        result = "BOOTSTRAP_PASS fresh=" + std::to_string(proof.fresh)
                + " presents=" + std::to_string(proof.host->report().presentedFrames)
                + " pcm_frames=" + std::to_string(proof.audioFrames)
                + " pcm_peak=" + std::to_string(proof.pcm.metrics().peak)
                + " pcm_clipped=" + std::to_string(proof.pcm.metrics().clipped)
                + " pcm_dropped=" + std::to_string(proof.pcm.metrics().dropped)
                + " pcm_max_pending=" + std::to_string(proof.pcm.metrics().maxPending)
                + " input_reads=" + std::to_string(proof.inputReads)
                + " lifecycle_cycles=" + std::to_string(pauseCycles)
                + " resumed_fresh=" + std::to_string(proof.fresh - lastResumeFresh)
                + " paused_ee_cycles=" + std::to_string(stoppedCycles)
                + " unload_from_paused=" + std::to_string(verifyLifecycle ? 1 : 0)
                + " surface_rebinds=" + std::to_string(surfaceRebinds)
                + " fps_contract=" + std::to_string(proof.fps)
                + " game_identified=" + (proof.gameIdentified.load() ? "1" : "0")
                + " window_ms=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count());
    } catch (const std::exception& failure) { result = "BOOTSTRAP_FAIL reason=" + std::string(failure.what()); }
    // Keep the frontend interface/device alive while unload retracts its image,
    // flushes queues and joins the CPU/GS threads. Destroy the context last.
    if (loaded && unload) unload();
    if (reset && proof.hw.context_destroy) proof.hw.context_destroy();
    proof.host.reset();
    if (initialized && deinit) deinit();
    struct stat nvm{}, mec{};
    const bool nvmExists = stat((sidecar + ".nvm").c_str(), &nvm) == 0;
    if (result.rfind("BOOTSTRAP_PASS", 0) == 0
            && ((nvmExists && (!S_ISREG(nvm.st_mode) || nvm.st_size != 1024))
                || stat((sidecar + ".mec").c_str(), &mec) != 0
                || !S_ISREG(mec.st_mode) || mec.st_size != 4))
        result = "BOOTSTRAP_FAIL invalid BIOS sidecar after teardown";
    if (result.rfind("BOOTSTRAP_PASS", 0) == 0)
        result += " mec_persisted=1 nvm_file=" + std::string(nvmExists ? "present" : "absent_no_guest_write")
                + " recovered_default_logs=" + std::to_string(proof.nvramDefaults.load() + proof.mecDefaults.load());
    active.store(nullptr);
    if (library && dlclose(library) != 0) return "BOOTSTRAP_FAIL unconfirmed library teardown";
    result += " teardown_confirmed=1";
    return result;
}
} // namespace

extern "C" JNIEXPORT void JNICALL Java_EmuOrbitArmsBootstrap_confirmGuestImage(JNIEnv*, jclass) {
    guestImageConfirmed.store(true);
}

extern "C" JNIEXPORT jstring JNICALL Java_EmuOrbitArmsBootstrap_run(
        JNIEnv* env, jclass, jobject surface, jstring path, jint frames, jstring gamePath, jstring resourcePath,
        jstring systemPath, jstring biosDirectory, jstring biosName, jboolean verifyLifecycle, jobjectArray surfaces,
        jobject endpointTarget) {
    if (!surface || !path) return env->NewStringUTF("BOOTSTRAP_FAIL invalid arguments");
    const char* chars = env->GetStringUTFChars(path, nullptr);
    if (!chars) return nullptr;
    std::string root(chars);
    env->ReleaseStringUTFChars(path, chars);
    std::string game;
    if (gamePath) {
        const char* gameChars = env->GetStringUTFChars(gamePath, nullptr);
        if (!gameChars) return nullptr;
        game = gameChars;
        env->ReleaseStringUTFChars(gamePath, gameChars);
    }
    std::string resources;
    if (resourcePath) {
        const char* resourceChars = env->GetStringUTFChars(resourcePath, nullptr);
        if (!resourceChars) return nullptr;
        resources = resourceChars;
        env->ReleaseStringUTFChars(resourcePath, resourceChars);
    }
    std::string system, firmwareDirectory, firmwareName;
    for (auto pair : {std::make_pair(systemPath, &system), std::make_pair(biosDirectory, &firmwareDirectory),
            std::make_pair(biosName, &firmwareName)}) {
        if (!pair.first) continue;
        const char* text = env->GetStringUTFChars(pair.first, nullptr);
        if (!text) return nullptr;
        *pair.second = text;
        env->ReleaseStringUTFChars(pair.first, text);
    }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return env->NewStringUTF("BOOTSTRAP_FAIL no Surface");
    std::string result;
    std::array<ANativeWindow*, 2> replacements{};
    try {
        if (surfaces) {
            if (!verifyLifecycle || env->GetArrayLength(surfaces) != 2) throw std::runtime_error("Invalid Surface proof arguments");
            for (unsigned i = 0; i < replacements.size(); ++i) {
                auto object = env->GetObjectArrayElement(surfaces, i);
                if (!object) throw std::runtime_error("Missing replacement Surface");
                replacements[i] = ANativeWindow_fromSurface(env, object);
                env->DeleteLocalRef(object);
                if (!replacements[i]) throw std::runtime_error("No replacement NativeWindow");
            }
        }
        AudioBridge endpoint(env, endpointTarget);
        result = execute(window, root, frames, game, resources, system, firmwareDirectory, firmwareName,
                verifyLifecycle, replacements, endpoint);
    }
    catch (const std::exception& failure) { result = "BOOTSTRAP_FAIL reason=" + std::string(failure.what()); }
    for (auto* replacement : replacements) if (replacement) ANativeWindow_release(replacement);
    ANativeWindow_release(window);
    return env->NewStringUTF(result.c_str());
}
