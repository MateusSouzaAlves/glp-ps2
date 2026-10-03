// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent owner-only Libretro session. No APK/Activity/data paths baked in.
// A terminal CPU/GPU outcome retains every native object until process exit.
#include "libretro_frontend.h"
#include <cmath>
#include <cstdlib>
#include <limits.h>
#include <mutex>

namespace {
using namespace emuorbit::armsx2::frontend;
using Clock = std::chrono::steady_clock;
constexpr size_t kPcmBytes = emuorbit::armsx2::PcmMailbox::kCapacity * 4;
constexpr size_t kMetadata = 8;
struct Session {
    Frontend frontend;
    std::thread::id owner = std::this_thread::get_id();
    jlong id = 0;
    void* library = nullptr;
    void (*run)() = nullptr;
    void (*unload)() = nullptr;
    void (*deinit)() = nullptr;
    EmuOrbitArmsSetPaused setPaused = nullptr;
    EmuOrbitArmsSnapshot snapshot = nullptr;
    EmuOrbitArmsStateFile stateFile = nullptr;
    bool initialized = false, loadAttempted = false, reset = false;
    bool ready = false, paused = false, acquired = false, terminal = false;
    void require(int result) {
        if (result != EMUORBIT_ARMS_OK) {
            terminal = true;
            throw std::runtime_error("ARMSX2 lifecycle unconfirmed; process exit required");
        }
    }
    void pause() {
        require(setPaused(1)); paused = true;
        frontend.input.clear(); frontend.pcm.discard();
    }
    void close() {
        if (terminal) throw std::runtime_error("ARMSX2 lifetime quarantined");
        if (ready && !paused) pause();
        // Partial load may already own threads/settings, even on false return.
        if (loadAttempted) { unload(); loadAttempted = false; }
        if (reset && frontend.hw.context_destroy) frontend.hw.context_destroy();
        reset = false;
        if (frontend.host) {
            std::string error;
            // Destructor alone ignores vkDeviceWaitIdle's result. Confirm GPU
            // quiescence BEFORE declaring native release to Java/Window owner.
            if (!frontend.host->suspendPresentation(error)) {
                terminal = true; throw std::runtime_error("ARMSX2 final GPU idle unconfirmed: " + error);
            }
        }
        frontend.host.reset();
        if (initialized) { deinit(); initialized = false; }
        if (library && dlclose(library) != 0) { terminal = true; throw std::runtime_error("ARMSX2 dlclose unconfirmed"); }
        library = nullptr;
        active.store(nullptr);
    }
};
std::mutex registryMutex;
std::unique_ptr<Session> registered;
jlong sequence = 0;

std::string utf(JNIEnv* env, jstring value) {
    if (!value) throw std::invalid_argument("Missing ARMSX2 path");
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) throw std::runtime_error("Cannot access ARMSX2 path");
    std::string result;
    try { result = chars; } catch (...) { env->ReleaseStringUTFChars(value, chars); throw; }
    env->ReleaseStringUTFChars(value, chars);
    if (result.empty() || result.size() > 512) throw std::invalid_argument("ARMSX2 path length");
    return result;
}
void canonical(const std::string& path, bool directory) {
    char actual[PATH_MAX]; struct stat info{};
    if (path.front() != '/' || !realpath(path.c_str(), actual) || path != actual
            || lstat(path.c_str(), &info) != 0
            || (directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode)))
        throw std::invalid_argument("ARMSX2 path not canonical/regular");
}
void extent(jint width, jint height) {
    if (width < 1 || height < 1 || width > 8192 || height > 8192)
        throw std::invalid_argument("ARMSX2 Surface extent");
}
void exception(JNIEnv* env, const char* message) {
    if (env->ExceptionCheck()) return;
    jclass type = env->FindClass("java/io/IOException");
    if (type) { env->ThrowNew(type, message); env->DeleteLocalRef(type); }
}
Session& owner(jlong id, bool live = true) {
    if (!registered || registered->id != id || registered->owner != std::this_thread::get_id())
        throw std::invalid_argument("ARMSX2 session handle/owner mismatch");
    if (live && registered->terminal) throw std::runtime_error("ARMSX2 session terminal");
    return *registered;
}
template<class F> void operate(JNIEnv* env, jlong id, F operation) {
    std::lock_guard<std::mutex> guard(registryMutex);
    Session* session = nullptr;
    try { session = &owner(id); operation(*session); }
    catch (const std::invalid_argument& error) { exception(env, error.what()); }
    catch (const std::exception& error) {
        if (session) session->terminal = true;
        exception(env, error.what());
    } catch (...) {
        if (session) session->terminal = true;
        exception(env, "ARMSX2 unknown native failure; process exit required");
    }
}
} // namespace

#define JNI_NAME(name) Java_com_mateussouza_emuorbit_advance_emulator_playstation2_PlayStation2ArmsNativePort_##name
extern "C" JNIEXPORT jlong JNICALL JNI_NAME(openNative)(JNIEnv* env, jclass, jobject surface,
        jint width, jint height, jstring library, jstring system, jstring resources,
        jstring biosDirectory, jstring biosName, jstring game) {
    std::lock_guard<std::mutex> guard(registryMutex);
    ANativeWindow* window = nullptr;
    bool ownsOpen = false;
    try {
        if (registered || active.load()) throw std::invalid_argument("ARMSX2 native VM already admitted");
        extent(width, height);
        const auto core = utf(env, library), data = utf(env, system), assets = utf(env, resources);
        const auto firmware = utf(env, biosDirectory), name = utf(env, biosName);
        const auto media = game ? utf(env, game) : std::string();
        canonical(core, false); canonical(data, true); canonical(assets, true); canonical(firmware, true);
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos
                || name == "." || name == "..") throw std::invalid_argument("ARMSX2 BIOS basename");
        canonical(firmware + "/" + name, false);
        if (!media.empty()) canonical(media, false);
        if (!surface || !(window = ANativeWindow_fromSurface(env, surface)))
            throw std::invalid_argument("ARMSX2 missing Surface");
        auto session = std::make_unique<Session>();
        if (sequence == INT64_MAX) throw std::runtime_error("ARMSX2 handle sequence exhausted");
        session->id = ++sequence;
        auto& p = session->frontend;
        p.system = data; p.game = media;
        p.biosBase = name.substr(0, name.size() > 4 ? name.size() - 4 : name.size());
        const auto sidecar = firmware + "/" + p.biosBase;
        p.nvmWasMissing = access((sidecar + ".nvm").c_str(), F_OK) != 0;
        p.mecWasMissing = access((sidecar + ".mec").c_str(), F_OK) != 0;
        p.overrides["emuorbit_armsx2_resources"] = assets;
        p.overrides["emuorbit_armsx2_bios_directory"] = firmware;
        p.overrides["armsx2_bios"] = name;
        registered = std::move(session);
        ownsOpen = true;
        active.store(&p);
        auto& s = *registered;
        s.library = dlopen(core.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!s.library) throw std::runtime_error(dlerror());
        if (symbol<EmuOrbitArmsHostVersion>(s.library, "retro_emuorbit_host_abi_version")() != EMUORBIT_ARMS_HOST_ABI)
            throw std::runtime_error("ARMSX2 host ABI mismatch");
        if (symbol<unsigned(*)()>(s.library, "retro_api_version")() != RETRO_API_VERSION)
            throw std::runtime_error("ARMSX2 Libretro ABI mismatch");
        retro_system_info info{};
        symbol<void(*)(retro_system_info*)>(s.library, "retro_get_system_info")(&info);
        if (!info.library_name || !info.library_version || std::strcmp(info.library_name, "ARMSX2")
                || std::strcmp(info.library_version, "2.7.2")) throw std::runtime_error("ARMSX2 candidate descriptor mismatch");
        s.deinit = symbol<void(*)()>(s.library, "retro_deinit");
        s.unload = symbol<void(*)()>(s.library, "retro_unload_game");
        s.run = symbol<void(*)()>(s.library, "retro_run");
        s.setPaused = symbol<EmuOrbitArmsSetPaused>(s.library, "retro_emuorbit_set_paused_v1");
        s.snapshot = symbol<EmuOrbitArmsSnapshot>(s.library, "retro_emuorbit_snapshot_v1");
        s.stateFile = symbol<EmuOrbitArmsStateFile>(s.library, "retro_emuorbit_state_file_v1");
        symbol<void(*)(retro_environment_t)>(s.library, "retro_set_environment")(environment);
        symbol<void(*)(retro_video_refresh_t)>(s.library, "retro_set_video_refresh")(video);
        symbol<void(*)(retro_audio_sample_t)>(s.library, "retro_set_audio_sample")(audio);
        symbol<void(*)(retro_audio_sample_batch_t)>(s.library, "retro_set_audio_sample_batch")(audioBatch);
        symbol<void(*)(retro_input_poll_t)>(s.library, "retro_set_input_poll")(pollInput);
        symbol<void(*)(retro_input_state_t)>(s.library, "retro_set_input_state")(input);
        s.initialized = true; symbol<void(*)()>(s.library, "retro_init")();
        const retro_game_info content{media.c_str(), nullptr, 0, nullptr};
        s.loadAttempted = true;
        if (!symbol<bool(*)(const retro_game_info*)>(s.library, "retro_load_game")(media.empty() ? nullptr : &content))
            throw std::runtime_error("ARMSX2 rejected media/BIOS");
        if (!p.negotiation) throw std::runtime_error("ARMSX2 Vulkan negotiation missing");
        std::string error;
        p.host = VulkanRenderHost::create(window, width, height,
                reinterpret_cast<const VulkanNegotiationInterface*>(p.negotiation), error);
        if (!p.host) throw std::runtime_error("ARMSX2 Vulkan: " + error);
        s.reset = true; p.hw.context_reset();
        // load_game returns before CPU boot. Wait for valid VM without presenting
        // fake startup frames; outer process watchdog handles an upstream hang.
        const auto deadline = Clock::now() + std::chrono::seconds(20);
        int status;
        while ((status = s.setPaused(0)) == EMUORBIT_ARMS_NOT_READY && !p.errors.load() && Clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (status != EMUORBIT_ARMS_OK || p.errors.load()) {
            s.terminal = true; // Boot/lifecycle result unknown: never unload a late CPU closure.
            throw std::runtime_error("ARMSX2 initial VM boot not confirmed");
        }
        s.ready = true;
        ANativeWindow_release(window); window = nullptr;
        return s.id;
    } catch (const std::exception& failed) {
        if (window) ANativeWindow_release(window);
        if (ownsOpen && registered) {
            try { registered->close(); registered.reset(); }
            catch (...) { registered->terminal = true; } // Keep frontend/window/library and admission.
        }
        exception(env, failed.what()); return 0;
    } catch (...) {
        if (window) ANativeWindow_release(window);
        if (ownsOpen && registered) registered->terminal = true;
        exception(env, "ARMSX2 open failed; native admission may be retained"); return 0;
    }
}
extern "C" JNIEXPORT void JNICALL JNI_NAME(pauseNative)(JNIEnv* env, jclass, jlong id, jboolean paused) {
    operate(env, id, [paused](Session& s) {
        if (paused) s.pause();
        else {
            if (!s.frontend.host->hasPresentation()) throw std::invalid_argument("ARMSX2 resume without Surface");
            s.require(s.setPaused(0)); s.paused = false;
        }
    });
}
extern "C" JNIEXPORT void JNICALL JNI_NAME(replaceNative)(JNIEnv* env, jclass, jlong id,
        jobject surface, jint width, jint height) {
    // Reject foreign owner/preconditions before acquiring or mutating Window.
    operate(env, id, [=](Session& s) {
        if (!s.paused) throw std::invalid_argument("ARMSX2 replace requires pause");
        if (surface) extent(width, height);
        ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
        if (surface && !window) throw std::invalid_argument("ARMSX2 replacement Surface unavailable");
        std::string error;
        auto* interface = s.frontend.host->interface();
        const auto device = interface->device;
        const bool parked = s.frontend.host->suspendPresentation(error);
        s.acquired = false;
        bool attached = parked;
        if (parked && window) attached = s.frontend.host->replacePresentation(window, width, height, error);
        if (window) ANativeWindow_release(window);
        if (!attached) throw std::runtime_error("ARMSX2 GPU replacement unconfirmed: " + error);
        if (s.frontend.host->interface() != interface || interface->device != device)
            throw std::runtime_error("ARMSX2 presentation replacement changed native device");
    });
}
extern "C" JNIEXPORT void JNICALL JNI_NAME(frameNative)(JNIEnv* env, jclass, jlong id, jobject pcm,
        jlongArray metadata, jint buttons, jint lx, jint ly, jint rx, jint ry) {
    operate(env, id, [=](Session& s) {
        auto& p = s.frontend;
        if (!pcm) throw std::invalid_argument("ARMSX2 PCM buffer missing");
        auto* destination = static_cast<int16_t*>(env->GetDirectBufferAddress(pcm));
        if (!destination || env->GetDirectBufferCapacity(pcm) != kPcmBytes || !metadata
                || env->GetArrayLength(metadata) != kMetadata)
            throw std::invalid_argument("ARMSX2 PCM/metadata transport ABI");
        if (s.paused || !p.host->hasPresentation()) throw std::invalid_argument("ARMSX2 frame without running Surface");
        if (!p.input.set(buttons, lx, ly, rx, ry)) throw std::invalid_argument("ARMSX2 input range");
        std::string error;
        if (!s.acquired && !p.host->beginCoreFrame(error)) throw std::runtime_error("ARMSX2 acquire: " + error);
        s.acquired = true;
        const auto before = p.fresh;
        const auto begin = Clock::now();
        s.run(); const auto coreDone = Clock::now();
        const auto copied = p.pcm.drain(destination, emuorbit::armsx2::PcmMailbox::kCapacity);
        const bool fresh = before != p.fresh;
        if (fresh) {
            if (!p.host->presentCoreFrame(p.width, p.height, error)) throw std::runtime_error("ARMSX2 present: " + error);
            s.acquired = false;
        }
        if (p.softwareFrame || p.errors.load() || p.rate != 48000 || !std::isfinite(p.fps)
                || (std::abs(p.fps - 50) >= 1 && std::abs(p.fps - 60) >= 1))
            throw std::runtime_error("ARMSX2 invalid runtime video/audio contract");
        const jlong values[kMetadata] = {static_cast<jlong>(copied), fresh ? 1 : 0,
                std::chrono::duration_cast<std::chrono::nanoseconds>(coreDone - begin).count(),
                fresh ? std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - coreDone).count() : 0,
                48000, static_cast<jlong>(std::llround(p.fps * 1000000)),
                static_cast<jlong>(p.errors.load()), static_cast<jlong>(p.fresh)};
        env->SetLongArrayRegion(metadata, 0, kMetadata, values);
        if (env->ExceptionCheck()) throw std::runtime_error("ARMSX2 metadata delivery failed");
    });
}
extern "C" JNIEXPORT jlong JNICALL JNI_NAME(pausedCyclesNative)(JNIEnv* env, jclass, jlong id) {
    jlong cycles = -1;
    operate(env, id, [&](Session& s) {
        if (!s.paused) throw std::invalid_argument("ARMSX2 cycle snapshot requires pause");
        unsigned state = 0; uint64_t value = 0; s.require(s.snapshot(&state, &value));
        if (state != EMUORBIT_ARMS_PAUSED) throw std::runtime_error("ARMSX2 cycle snapshot state mismatch");
        cycles = static_cast<jlong>(value);
    }); return cycles;
}
extern "C" JNIEXPORT jboolean JNICALL JNI_NAME(terminalNative)(JNIEnv* env, jclass, jlong id) {
    std::lock_guard<std::mutex> guard(registryMutex);
    try { return owner(id, false).terminal; }
    catch (const std::exception& failed) { exception(env, failed.what()); return true; }
}
extern "C" JNIEXPORT jboolean JNICALL JNI_NAME(stateFileNative)(JNIEnv* env, jclass, jlong id,
        jstring file, jboolean loading) {
    bool result = false;
    operate(env, id, [=, &result](Session& s) {
        if (!s.paused) throw std::invalid_argument("ARMSX2 state requires confirmed pause");
        const auto path = utf(env, file);
        const auto prefix = s.frontend.system + "/.state-work-";
        // Restrict the native writer/loader to one regular, already-created
        // scratch file in this VM's system directory. Never accept media/card,
        // an external document or a committed state slot as a target.
        if (path.rfind(prefix, 0) != 0)
            throw std::invalid_argument("ARMSX2 state scratch scope");
        const auto suffix = path.substr(prefix.size());
        const auto slash = suffix.find('/');
        if (slash == 0 || slash == std::string::npos || suffix.substr(slash) != "/state.zip")
            throw std::invalid_argument("ARMSX2 state scratch scope");
        canonical(path, false);
        struct stat scratch{};
        if (lstat(path.c_str(), &scratch) != 0 || scratch.st_nlink != 1)
            throw std::invalid_argument("ARMSX2 state scratch must not alias another file");
        const int status = s.stateFile(path.c_str(), loading ? 1u : 0u);
        if (!loading && status == EMUORBIT_ARMS_REFUSED) return; // paused serializer refusal
        s.require(status); result = true;
        s.frontend.input.clear(); s.frontend.pcm.discard();
        s.frontend.fresh = 0; // no stale presentation masquerades as a restored frame
    });
    return result;
}
extern "C" JNIEXPORT void JNICALL JNI_NAME(closeNative)(JNIEnv* env, jclass, jlong id) {
    std::lock_guard<std::mutex> guard(registryMutex);
    Session* session = nullptr;
    try { session = &owner(id); session->close(); registered.reset(); }
    catch (const std::exception& failed) { if (session) session->terminal = true; exception(env, failed.what()); }
    catch (...) { if (session) session->terminal = true; exception(env, "ARMSX2 teardown unconfirmed"); }
}
extern "C" JNIEXPORT jboolean JNICALL JNI_NAME(idleNative)(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(registryMutex);
    return !registered && !active.load();
}
