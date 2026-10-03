// SPDX-License-Identifier: GPL-3.0-or-later
// Shared private Libretro frontend for bootstrap and persistent sessions.
// One admitted owner; callbacks never call Java or write Android audio.
#pragma once
#define VK_USE_PLATFORM_ANDROID_KHR
#define VK_NO_PROTOTYPES
#include "libretro.h"
#include "libretro_vulkan.h"
#include "vulkan_render_host.h"
#include "pcm_mailbox.h"
#include "input_snapshot.h"
#include "host_api.h"

#include <android/native_window_jni.h>
#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace emuorbit::armsx2::frontend {
using emuorbit::n3ds::VulkanRenderHost;
using emuorbit::n3ds::VulkanNegotiationInterface;
using emuorbit::n3ds::VulkanRenderInterface;
// Audit actual upstream declarations at compile time, not a guessed version.
#define ABI_FIELD(ours, upstream, a, b) static_assert(offsetof(ours, a) == offsetof(upstream, b))
static_assert(sizeof(VulkanNegotiationInterface) == sizeof(retro_hw_render_context_negotiation_interface_vulkan));
ABI_FIELD(VulkanNegotiationInterface, retro_hw_render_context_negotiation_interface_vulkan, interfaceType, interface_type);
ABI_FIELD(VulkanNegotiationInterface, retro_hw_render_context_negotiation_interface_vulkan, interfaceVersion, interface_version);
ABI_FIELD(VulkanNegotiationInterface, retro_hw_render_context_negotiation_interface_vulkan, getApplicationInfo, get_application_info);
ABI_FIELD(VulkanNegotiationInterface, retro_hw_render_context_negotiation_interface_vulkan, createDevice, create_device);
ABI_FIELD(VulkanNegotiationInterface, retro_hw_render_context_negotiation_interface_vulkan, destroyDevice, destroy_device);
ABI_FIELD(VulkanNegotiationInterface, retro_hw_render_context_negotiation_interface_vulkan, createInstance, create_instance);
ABI_FIELD(VulkanNegotiationInterface, retro_hw_render_context_negotiation_interface_vulkan, createDevice2, create_device2);
static_assert(sizeof(VulkanRenderInterface) == sizeof(retro_hw_render_interface_vulkan));
ABI_FIELD(VulkanRenderInterface, retro_hw_render_interface_vulkan, setImage, set_image);
ABI_FIELD(VulkanRenderInterface, retro_hw_render_interface_vulkan, getSyncIndex, get_sync_index);
ABI_FIELD(VulkanRenderInterface, retro_hw_render_interface_vulkan, setCommandBuffers, set_command_buffers);
ABI_FIELD(VulkanRenderInterface, retro_hw_render_interface_vulkan, waitSyncIndex, wait_sync_index);
ABI_FIELD(VulkanRenderInterface, retro_hw_render_interface_vulkan, lockQueue, lock_queue);
ABI_FIELD(VulkanRenderInterface, retro_hw_render_interface_vulkan, setSignalSemaphore, set_signal_semaphore);
static_assert(sizeof(emuorbit::n3ds::VulkanImage) == sizeof(retro_vulkan_image));
ABI_FIELD(emuorbit::n3ds::VulkanImage, retro_vulkan_image, createInfo, create_info);
static_assert(sizeof(emuorbit::n3ds::VulkanContext) == sizeof(retro_vulkan_context));
ABI_FIELD(emuorbit::n3ds::VulkanContext, retro_vulkan_context, presentationQueueFamilyIndex, presentation_queue_family_index);
#undef ABI_FIELD

struct Frontend {
    std::string system;
    std::string game;
    std::string biosBase = "scph39001";
    std::unordered_map<std::string, std::string> options;
    std::unordered_map<std::string, std::string> overrides;
    retro_hw_render_callback hw{};
    const retro_hw_render_context_negotiation_interface_vulkan* negotiation = nullptr;
    std::unique_ptr<VulkanRenderHost> host;
    std::atomic<unsigned> errors{0};
    std::atomic<unsigned> nvramDefaults{0}, mecDefaults{0};
    std::atomic<bool> gameIdentified{false};
    bool nvmWasMissing = false, mecWasMissing = false;
    unsigned fresh = 0;
    unsigned width = 640, height = 448;
    uint64_t audioFrames = 0;
    emuorbit::armsx2::PcmMailbox pcm;
    emuorbit::armsx2::InputSnapshot input;
    uint64_t inputReads = 0;
    double rate = 48000.0, fps = 59.94;
    bool softwareFrame = false;
};
inline std::atomic<Frontend*> active{nullptr};

inline void log(enum retro_log_level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (level == RETRO_LOG_ERROR) std::fputs("PROBE_CORE_ERROR: ", stderr);
    char message[4096];
    std::vsnprintf(message, sizeof(message), format, args);
    std::fputs(message, stderr);
    va_end(args);
    if (level >= RETRO_LOG_WARN)
        __android_log_write(level == RETRO_LOG_ERROR ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN,
                "EmuOrbit-ARMSX2", message);
    if (auto* p = active.load()) {
        if (!p->game.empty() && std::string(message).rfind("Game changed: ", 0) == 0
                && std::string(message).rfind("Game changed: PS2 BIOS", 0) != 0)
            p->gameIdentified.store(true);
    }
    if (level == RETRO_LOG_ERROR) if (auto* p = active.load()) {
        // A clean BIOS root legitimately creates these two sidecars. Keep the
        // original ERROR visible, distinguish only the pinned ENOENT messages,
        // and later verify MEC/defaults after unload. NVM is only flushed by
        // the pinned core after a guest write, not merely after loading BIOS.
        // All other errors fail.
        const std::string text(message);
        if (p->nvmWasMissing && text.find("errno 2: No such file or directory") != std::string::npos
                && text.rfind("Failed to open or read NVRAM at " + p->biosBase + ".nvm:", 0) == 0)
            ++p->nvramDefaults;
        else if (p->mecWasMissing && text.find("errno 2: No such file or directory, creating default.") != std::string::npos
                && text.rfind("Failed to open or read MEC file at " + p->biosBase + ".nvm:", 0) == 0)
            ++p->mecDefaults;
        else ++p->errors;
    }
}

inline bool environment(unsigned command, void* data) {
    auto* p = active.load();
    if (!p) return false;
    switch (command) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = log; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = p->system.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 2; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        const auto* options = static_cast<retro_core_options_v2*>(data);
        for (const auto* d = options->definitions; d && d->key; ++d)
            if (d->default_value) p->options[d->key] = d->default_value;
        p->options["armsx2_renderer"] = "Vulkan";
        p->options["armsx2_upscale"] = "1x";
        p->options["armsx2_show_fps"] = "disabled";
        p->options["armsx2_fast_boot"] = p->game.empty() ? "disabled" : "enabled";
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* variable = static_cast<retro_variable*>(data);
        auto override = p->overrides.find(variable->key ? variable->key : "");
        if (override != p->overrides.end()) { variable->value = override->second.c_str(); return true; }
        auto found = p->options.find(variable->key ? variable->key : "");
        variable->value = found == p->options.end() ? nullptr : found->second.c_str();
        return variable->value != nullptr;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *static_cast<int*>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        const auto* hw = static_cast<retro_hw_render_callback*>(data);
        if (hw->context_type != RETRO_HW_CONTEXT_VULKAN || !hw->context_reset) return false;
        p->hw = *hw; return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: {
        const auto* n = static_cast<retro_hw_render_context_negotiation_interface_vulkan*>(data);
        if (n->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN
                || n->interface_version != 2 || !n->get_application_info || !n->create_device
                || n->create_instance || n->create_device2) return false;
        p->negotiation = n; return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
        if (!p->host) return false;
        *static_cast<const void**>(data) = p->host->interface(); return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const auto* av = static_cast<retro_system_av_info*>(data);
        p->rate = av->timing.sample_rate; p->fps = av->timing.fps; return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY: return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: return true;
    default: return false;
    }
}
inline void video(const void* data, unsigned width, unsigned height, size_t) {
    if (auto* p = active.load()) {
        p->width = width; p->height = height;
        if (data == RETRO_HW_FRAME_BUFFER_VALID) ++p->fresh;
        else if (data) p->softwareFrame = true;
    }
}
inline void audio(int16_t left, int16_t right) {
    if (auto* p = active.load()) {
        const int16_t pair[]{left, right};
        ++p->audioFrames; if (!p->pcm.push(pair, 1)) ++p->errors;
    }
}
inline size_t audioBatch(const int16_t* source, size_t frames) {
    if (auto* p = active.load()) {
        p->audioFrames += frames; if (!p->pcm.push(source, frames)) ++p->errors;
    }
    return frames; // Copied transport admission is not an auditory quality certificate.
}
inline void pollInput() {}
inline int16_t input(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (auto* p = active.load()) { ++p->inputReads; return p->input.read(port, device, index, id); }
    return 0;
}

template<class T> T symbol(void* library, const char* name) {
    auto result = reinterpret_cast<T>(dlsym(library, name));
    if (!result) throw std::runtime_error(std::string("Missing core export: ") + name);
    return result;
}

} // namespace emuorbit::armsx2::frontend
