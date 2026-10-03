/* SPDX-License-Identifier: MIT
 * Private Android loader proof. No JNI/UI, game boot, firmware or storage writes.
 * Uses the upstream MIT Libretro declarations; full notice stays in libretro.h.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "libretro.h"
#include "host_api.h"

int main(int argc, char** argv) {
    if (argc != 2 || argv[1][0] != '/' || strnlen(argv[1], 4097) > 4096) {
        fprintf(stderr, "Usage: armsx2_abi_probe /absolute/private/core.so\n");
        return 1;
    }
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "LOAD_FAIL: %s\n", dlerror());
        return 2;
    }
    const char* required[] = {
        "retro_api_version", "retro_init", "retro_deinit", "retro_get_system_info",
        "retro_get_system_av_info", "retro_set_environment", "retro_set_video_refresh",
        "retro_set_audio_sample", "retro_set_audio_sample_batch", "retro_set_input_poll",
        "retro_set_input_state", "retro_set_controller_port_device", "retro_reset", "retro_run",
        "retro_serialize_size", "retro_serialize", "retro_unserialize", "retro_cheat_reset",
        "retro_cheat_set", "retro_load_game", "retro_load_game_special", "retro_unload_game",
        "retro_get_region", "retro_get_memory_data", "retro_get_memory_size",
        "retro_emuorbit_host_abi_version", "retro_emuorbit_set_paused_v1", "retro_emuorbit_snapshot_v1"
    };
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (!dlsym(library, required[i])) {
            fprintf(stderr, "MISSING_EXPORT: %s\n", required[i]);
            dlclose(library);
            return 3;
        }
    }
    unsigned (*api_version)(void) = (unsigned (*)(void))dlsym(library, "retro_api_version");
    void (*system_info)(struct retro_system_info*) =
        (void (*)(struct retro_system_info*))dlsym(library, "retro_get_system_info");
    void (*av_info)(struct retro_system_av_info*) =
        (void (*)(struct retro_system_av_info*))dlsym(library, "retro_get_system_av_info");
    struct retro_system_info info = {0};
    struct retro_system_av_info av = {0};
    system_info(&info);
    av_info(&av);
    int valid = api_version() == RETRO_API_VERSION && info.library_name && info.library_version
        && strcmp(info.library_name, "ARMSX2") == 0 && strcmp(info.library_version, "2.7.2") == 0
        && info.need_fullpath && info.block_extract && av.timing.sample_rate == 48000.0
        && av.geometry.base_width > 0 && av.geometry.base_height > 0;
    EmuOrbitArmsHostVersion host_version = (EmuOrbitArmsHostVersion)dlsym(library, "retro_emuorbit_host_abi_version");
    EmuOrbitArmsSetPaused pause = (EmuOrbitArmsSetPaused)dlsym(library, "retro_emuorbit_set_paused_v1");
    EmuOrbitArmsSnapshot snapshot = (EmuOrbitArmsSnapshot)dlsym(library, "retro_emuorbit_snapshot_v1");
    unsigned state = 77;
    uint64_t cycles = 123;
    valid = valid && host_version() == EMUORBIT_ARMS_HOST_ABI
        && pause(2) == EMUORBIT_ARMS_INVALID_ARGUMENT
        && pause(1) == EMUORBIT_ARMS_WRONG_OWNER
        && snapshot(NULL, &cycles) == EMUORBIT_ARMS_INVALID_ARGUMENT
        && snapshot(&state, &cycles) == EMUORBIT_ARMS_WRONG_OWNER && state == 77 && cycles == 123;
    if (valid) {
        printf("ABI_LOAD_PASS name=%s version=%s api=%u audio_rate=%.0f initial_fps=%.2f "
            "base=%ux%u fullpath=%u block_extract=%u private_host_abi=1 scope=ABI_ONLY_NOT_BOOT_OR_PERFORMANCE\n",
            info.library_name, info.library_version, api_version(), av.timing.sample_rate,
            av.timing.fps, av.geometry.base_width, av.geometry.base_height,
            (unsigned)info.need_fullpath, (unsigned)info.block_extract);
    } else {
        fprintf(stderr, "ABI_CONTRACT_FAIL\n");
    }
    int closed = dlclose(library);
    return valid && closed == 0 ? 0 : 4;
}
