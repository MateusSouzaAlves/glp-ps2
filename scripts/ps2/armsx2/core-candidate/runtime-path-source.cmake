# SPDX-License-Identifier: MIT
# Minimal frontend seam, not a VM/render/audio change. A verified host may
# bind immutable resources separately from its persistent system/card paths.
set(original "${ARMSX2_SOURCE_DIR}/pcsx2-libretro/Main.cpp")
file(SHA256 "${original}" original_hash)
if(NOT original_hash STREQUAL "5f6a5bb5fa4ae992632d7cae0a1a3d531e22f5eec940e6fbebbe4e584a8d451c")
    message(FATAL_ERROR "Private runtime path frontend revision changed; re-audit")
endif()
file(READ "${original}" frontend)
set(resource_anchor "\tEmuFolders::Resources = Path::Combine(EmuFolders::AppRoot, \"resources\");\n")
set(bios_anchor "\t\ts_base_settings->SetBoolValue(\"InputSources\", \"SDL\", false);\n")
foreach(anchor IN ITEMS "${resource_anchor}" "${bios_anchor}")
    string(FIND "${frontend}" "${anchor}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Private runtime path frontend anchor changed")
    endif()
endforeach()
set(resources_override [=[
	// EmuOrbit host-only binding. No persisted core option, download or fallback
	// after an explicit invalid directory. The host verifies the full inventory.
	retro_variable private_resources{"emuorbit_armsx2_resources", nullptr};
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &private_resources) && private_resources.value)
	{
		if (std::strlen(private_resources.value) > 512 || !Path::IsAbsolute(private_resources.value) ||
			!FileSystem::DirectoryExists(private_resources.value))
		{
			log_cb(RETRO_LOG_ERROR, "Invalid private resource directory.\n");
			return false;
		}
		EmuFolders::Resources = private_resources.value;
		log_cb(RETRO_LOG_INFO, "EmuOrbit private resource root: %s\n", private_resources.value);
	}
]=])
set(bios_override [=[
		// Firmware may have its own verified, persistent store. NVM/MEC remain
		// associated with the selected BIOS basename, not a resource epoch.
		retro_variable private_bios{"emuorbit_armsx2_bios_directory", nullptr};
		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &private_bios) && private_bios.value)
		{
			if (std::strlen(private_bios.value) > 512 || !Path::IsAbsolute(private_bios.value) ||
				!FileSystem::DirectoryExists(private_bios.value))
			{
				log_cb(RETRO_LOG_ERROR, "Invalid private BIOS directory.\n");
				return false;
			}
			s_base_settings->SetStringValue("Folders", "Bios", private_bios.value);
		}
]=])
string(REPLACE "${resource_anchor}" "${resource_anchor}${resources_override}" frontend "${frontend}")
string(REPLACE "${bios_anchor}" "${bios_anchor}${bios_override}" frontend "${frontend}")
set(osd_anchor "\t\ts_base_settings->SetBoolValue(\"InputSources\", \"SDL\", false);\n")
# Owner requested a clean game image (including blending and AA diagnostics).
# Disable only visual overlays; native logs/errors and Android load failures stay.
set(osd_settings "\t\ts_base_settings->SetIntValue(\"EmuCore/GS\", \"OsdMessagesPos\", 0);\n\t\ts_base_settings->SetIntValue(\"EmuCore/GS\", \"OsdPerformancePos\", 0);\n")
foreach(flag IN ITEMS OsdShowSpeed OsdShowFPS OsdShowVPS OsdShowResolution OsdShowGSStats
        OsdShowCPU OsdShowGPU OsdShowGPUDebug OsdShowGPUStats OsdShowIndicators OsdShowFrameTimes
        OsdShowHardwareInfo OsdShowVersion OsdShowSettings OsdshowPatches OsdShowInputs
        OsdShowInputRec OsdShowTextureReplacements)
    string(APPEND osd_settings "\t\ts_base_settings->SetBoolValue(\"EmuCore/GS\", \"${flag}\", false);\n")
endforeach()
string(REPLACE "${osd_anchor}" "${osd_anchor}${osd_settings}" frontend "${frontend}")
set(ARMSX2_LIBRETRO_MAIN "${CMAKE_BINARY_DIR}/derived/armsx2/Main.cpp")
file(WRITE "${ARMSX2_LIBRETRO_MAIN}" "${frontend}")
message(STATUS "Private resource/BIOS directory frontend derived; verified checkout unchanged")
