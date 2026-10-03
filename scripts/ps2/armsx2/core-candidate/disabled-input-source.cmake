# SPDX-License-Identifier: MIT
# The pinned core constructs/initializes SDL before checking whether it is
# enabled. A Libretro host supplies input itself; do not initialize a disabled
# source just to shut it down. Existing-source disable/re-enable stays upstream.
set(original "${ARMSX2_SOURCE_DIR}/pcsx2/Input/InputManager.cpp")
file(SHA256 "${original}" original_hash)
if(NOT original_hash STREQUAL "8c42e723b6efbcc37b48667952ab2d88829bef3e7c7fb4005761a4c5e1d41630")
    message(FATAL_ERROR "Input source patch revision changed; re-audit")
endif()
file(READ "${original}" input_source)
set(anchor "void InputManager::UpdateInputSourceState(SettingsInterface& si, std::unique_lock<std::mutex>& settings_lock, InputSourceType type)\n{\n")
string(FIND "${input_source}" "${anchor}" anchor_pos)
if(anchor_pos EQUAL -1)
    message(FATAL_ERROR "Input source patch anchor changed")
endif()
set(guard "\t// EmuOrbit: no initialization of a disabled, absent input source.\n\tif (!s_input_sources[static_cast<u32>(type)] && !IsInputSourceEnabled(si, type))\n\t\treturn;\n\n")
string(REPLACE "${anchor}" "${anchor}${guard}" input_source "${input_source}")
# Binding parsers/enumerators assumed ReloadSources constructed every source.
# An absent disabled source is valid; never dereference it while parsing saved
# keyboard/controller bindings. Live initialized sources retain their behavior.
set(unsafe_check "if (s_input_sources[i]->IsInitialized())")
string(REGEX MATCHALL "if \\(s_input_sources\\[i\\]->IsInitialized\\(\\)\\)" unsafe_checks "${input_source}")
list(LENGTH unsafe_checks unsafe_check_count)
if(NOT unsafe_check_count EQUAL 6)
    message(FATAL_ERROR "Input binding null-source checks changed; re-audit")
endif()
string(REPLACE "${unsafe_check}" "if (s_input_sources[i] && s_input_sources[i]->IsInitialized())" input_source "${input_source}")
set(mapping_check "if (s_input_sources[i]->IsInitialized() && s_input_sources[i]->GetGenericBindingMapping(device, &mapping))")
string(FIND "${input_source}" "${mapping_check}" mapping_pos)
if(mapping_pos EQUAL -1)
    message(FATAL_ERROR "Input mapping null-source check changed; re-audit")
endif()
string(REPLACE "${mapping_check}" "if (s_input_sources[i] && s_input_sources[i]->IsInitialized() && s_input_sources[i]->GetGenericBindingMapping(device, &mapping))" input_source "${input_source}")
set(derived "${CMAKE_BINARY_DIR}/derived/armsx2/InputManager.cpp")
file(WRITE "${derived}" "${input_source}")
get_target_property(core_sources PCSX2 SOURCES)
if(NOT "Input/InputManager.cpp" IN_LIST core_sources)
    message(FATAL_ERROR "Input source target path changed")
endif()
list(REMOVE_ITEM core_sources "Input/InputManager.cpp")
list(APPEND core_sources "${derived}")
set_property(TARGET PCSX2 PROPERTY SOURCES "${core_sources}")
set_source_files_properties("${derived}" PROPERTIES
    INCLUDE_DIRECTORIES "${ARMSX2_SOURCE_DIR}/pcsx2/Input")
