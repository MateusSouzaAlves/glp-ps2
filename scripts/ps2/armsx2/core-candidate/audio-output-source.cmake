# SPDX-License-Identifier: MIT
# Main.cpp was pinned by runtime-path-source. Keep original SPU2/GS/guest timing.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../host/audio_peak_guard.h")
set(audio_anchor "\t\t\tfor (u32 i = 0; i < frames * 2; i++)\n\t\t\t\tabuf[i] = static_cast<s16>(std::clamp(fbuf[i], -1.0f, 1.0f) * 32767.0f);\n")
string(FIND "${frontend}" "${audio_anchor}" position)
if(position EQUAL -1)
    message(FATAL_ERROR "ARMSX2 float PCM conversion changed; re-audit")
endif()
string(REPLACE "#include \"pcsx2/Host/AudioStream.h\"\n"
    "#include \"pcsx2/Host/AudioStream.h\"\n#include \"audio_peak_guard.h\"\nstatic emuorbit::armsx2::AudioPeakGuard s_emuorbit_audio_peak_guard;\n" frontend "${frontend}")
string(REPLACE "${audio_anchor}" "\t\t\ts_emuorbit_audio_peak_guard.convert(fbuf, abuf, frames);\n" frontend "${frontend}")
string(REPLACE "RETRO_API bool retro_load_game(const struct retro_game_info* game)\n{\n"
    "RETRO_API bool retro_load_game(const struct retro_game_info* game)\n{\n\ts_emuorbit_audio_peak_guard.reset();\n" frontend "${frontend}")
file(WRITE "${ARMSX2_LIBRETRO_MAIN}" "${frontend}")
