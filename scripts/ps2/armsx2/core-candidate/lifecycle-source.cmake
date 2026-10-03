# SPDX-License-Identifier: MIT
# Apply only after runtime-path-source.cmake's original hash check. This emits
# a derived GPL frontend with a versioned private lifecycle extension.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/lifecycle-extension.inc")
file(READ "${CMAKE_CURRENT_LIST_DIR}/lifecycle-extension.inc" extension)
set(include_anchor "#include \"pcsx2/VMManager.h\"\n")
set(load_anchor "RETRO_API bool retro_load_game(const struct retro_game_info* game)\n{\n")
foreach(anchor IN ITEMS "${include_anchor}" "${load_anchor}")
    string(FIND "${frontend}" "${anchor}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Private lifecycle frontend anchor changed; re-audit")
    endif()
endforeach()
set(load_guard [=[
	// A terminal timeout can leave asynchronous CPU/GS work alive. Never admit
	// another VM in that process or reset the terminal flag on a new load.
	if (s_emuorbit_terminal.load(std::memory_order_acquire) || LibretroCore::s_cpu_thread.joinable())
		return false;
	s_emuorbit_owner.store(std::this_thread::get_id(), std::memory_order_release);
]=])
# The extension depends on LibretroCore/Host definitions, so keep it at the end
# and only introduce declarations before load_game.
string(REPLACE "${include_anchor}"
    "${include_anchor}#include \"pcsx2/R5900.h\"\n#include \"cpu_reply.h\"\nstatic std::atomic<bool> s_emuorbit_terminal{false};\nstatic std::atomic<std::thread::id> s_emuorbit_owner{};\n"
    frontend "${frontend}")
string(REPLACE "${load_anchor}" "${load_anchor}${load_guard}" frontend "${frontend}")
string(APPEND frontend "\n${extension}\n")
file(WRITE "${ARMSX2_LIBRETRO_MAIN}" "${frontend}")
message(STATUS "Private lifecycle ABI v1 frontend derived; verified checkout unchanged")
