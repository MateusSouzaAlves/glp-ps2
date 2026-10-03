# SPDX-License-Identifier: MIT
# Vulkan is the only host-negotiated renderer in this Android integration.
# Omit only the desktop "choose Automatic" false positive, not unsafe-settings,
# device, compatibility or state errors. All other upstream diagnostics remain.
set(original "${ARMSX2_SOURCE_DIR}/pcsx2/VMManager.cpp")
file(SHA256 "${original}" original_hash)
if(NOT original_hash STREQUAL "cb81109b39c30c18a63c210f3b9792cefbc2f90727c35a7f8874a0894bf77093")
    message(FATAL_ERROR "Mobile OSD source changed; re-audit")
endif()
file(READ "${original}" manager_source)
set(anchor "#if !(defined(__APPLE__) && TARGET_OS_IPHONE)\n\t\t// iOS always renders with Metal;")
string(FIND "${manager_source}" "${anchor}" anchor_pos)
if(anchor_pos EQUAL -1)
    message(FATAL_ERROR "Renderer warning anchor changed")
endif()
string(REPLACE "${anchor}" "#if !(defined(__APPLE__) && TARGET_OS_IPHONE) && !defined(EMUORBIT_ARMSX2_VULKAN_ONLY)\n\t\t// iOS always renders with Metal;" manager_source "${manager_source}")
set(derived "${CMAKE_BINARY_DIR}/derived/armsx2/VMManager.cpp")
file(WRITE "${derived}" "${manager_source}")
get_target_property(core_sources PCSX2 SOURCES)
if(NOT "VMManager.cpp" IN_LIST core_sources)
    message(FATAL_ERROR "VM manager target source changed")
endif()
list(REMOVE_ITEM core_sources "VMManager.cpp")
list(APPEND core_sources "${derived}")
set_property(TARGET PCSX2 PROPERTY SOURCES "${core_sources}")
set_source_files_properties("${derived}" PROPERTIES
    INCLUDE_DIRECTORIES "${ARMSX2_SOURCE_DIR}/pcsx2"
    COMPILE_DEFINITIONS EMUORBIT_ARMSX2_VULKAN_ONLY=1)
