# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(EmuOrbitPS2DecoderHost LANGUAGES C CXX)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Product decoder requires Android ARM64")
endif()
# Keep the already validated disc/stream ABI, not the retired Play! VM.
foreach(pair "discimages/ChdImageStream.cpp|B4D840B525B3A3B5B938EE8E6F519AD16707953AE0434D1C37879EEBD9C98A41"
             "discimages/ChdStreamSupport.cpp|500971AEF9D23DAB53970DF5D9A71C3B181CEF1FD572325932C32AC8BF429BD1"
             "discimages/ChdCdImageStream.cpp|7B445F01591E0319420E8C46EAD67CBDFAF9BC8B3D82FF3A49F617A7D71D168E")
    string(REPLACE "|" ";" item "${pair}")
    list(GET item 0 path)
    list(GET item 1 expected)
    file(SHA256 "${PLAY_SOURCE}/Source/${path}" actual)
    string(TOUPPER "${actual}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Pinned decoder ABI changed: ${path}")
    endif()
endforeach()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static decoder dependencies" FORCE)
set(WITH_SYSTEM_ZLIB ON CACHE BOOL "NDK zlib" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "No legacy codec" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Static codec" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "No tools" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "Bounded decoder" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${PLAY_SOURCE}/deps/Dependencies/zstd/build/cmake" zstd EXCLUDE_FROM_ALL)
add_subdirectory("${PLAY_SOURCE}/deps/libchdr" libchdr EXCLUDE_FROM_ALL)
include("${CMAKE_CURRENT_LIST_DIR}/../media-proof/patch-libchdr-read.cmake")
add_library(ps2_chd_bridge SHARED
    ../adapters/chd_bridge.cpp
    ../media-proof/bounded_chd_stream.cpp
    ../media-proof/bounded_chd_optical.cpp
    "${PLAY_SOURCE}/deps/Framework/src/Stream.cpp")
target_compile_features(ps2_chd_bridge PRIVATE cxx_std_17)
target_compile_options(ps2_chd_bridge PRIVATE -Wall -Wextra -Werror)
target_include_directories(ps2_chd_bridge PRIVATE
    "${PLAY_SOURCE}/Source" "${PLAY_SOURCE}/deps/Framework/include")
target_link_libraries(ps2_chd_bridge PRIVATE chdr-static libzstd_static lzma)
target_link_options(ps2_chd_bridge PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
    "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/../preparation-proof/exports.map")
set_property(TARGET ps2_chd_bridge APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../preparation-proof/exports.map")
