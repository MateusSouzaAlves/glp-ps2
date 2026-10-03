# SPDX-License-Identifier: MIT
# Private derivative only. Do not change the installed 3DS host, renderer or
# lifecycle. The source/header still carry their GPL notice in generated files.
set(host_header "${EMUORBIT_ROOT}/nintendo3dscore/src/main/cpp/vulkan_render_host.h")
set(host_source "${EMUORBIT_ROOT}/nintendo3dscore/src/main/cpp/vulkan_render_host.cpp")
file(SHA256 "${host_header}" header_hash)
file(SHA256 "${host_source}" source_hash)
if(NOT header_hash STREQUAL "31133cb05886a1dcb19a3b11077233a1ec17e600f642060c32ad9b00c4643fe6"
        OR NOT source_hash STREQUAL "4b3f9b36fe5274597293fb70a31c6ed3574222469e6f7b0fc1359643d638369e")
    message(FATAL_ERROR "Reference Vulkan host changed; re-audit private Surface derivative")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${host_header}" "${host_source}" "${CMAKE_CURRENT_LIST_DIR}/surface-rebind.inc")
file(READ "${host_header}" header)
file(READ "${host_source}" implementation)
file(READ "${CMAKE_CURRENT_LIST_DIR}/surface-rebind.inc" extension)
set(header_anchor "    bool isOwnerThread() const;\n")
set(begin_anchor "bool VulkanRenderHost::beginCoreFrame(std::string& error) {\n")
foreach(entry IN ITEMS header begin)
    if(entry STREQUAL "header")
        string(FIND "${header}" "${header_anchor}" position)
    else()
        string(FIND "${implementation}" "${begin_anchor}" position)
    endif()
    if(position EQUAL -1)
        message(FATAL_ERROR "Reference Vulkan host Surface anchor changed")
    endif()
endforeach()
string(REPLACE "${header_anchor}" "${header_anchor}
    // Caller MUST confirm VM/GS paused before releasing/replacing presentation.
    // Device/context/guest resources and interface addresses remain unchanged.
    bool suspendPresentation(std::string& error);
    bool replacePresentation(ANativeWindow* window, uint32_t width, uint32_t height, std::string& error);
    bool hasPresentation() const { return window_ != nullptr && swapchain_ != VK_NULL_HANDLE; }
    uint32_t presentationWidth() const { return extent_.width; }
    uint32_t presentationHeight() const { return extent_.height; }
" header "${header}")
string(REPLACE "${begin_anchor}" "${begin_anchor}    if (!hasPresentation()) return fail(error, \"Vulkan presentation is suspended\");\n"
    implementation "${implementation}")
string(APPEND implementation "\n${extension}\n")
set(ARMSX2_PRIVATE_HOST_DIR "${CMAKE_BINARY_DIR}/derived/armsx2-vulkan-host")
set(ARMSX2_PRIVATE_HOST_SOURCE "${ARMSX2_PRIVATE_HOST_DIR}/vulkan_render_host.cpp")
file(WRITE "${ARMSX2_PRIVATE_HOST_DIR}/vulkan_render_host.h" "${header}")
file(WRITE "${ARMSX2_PRIVATE_HOST_SOURCE}" "${implementation}")
