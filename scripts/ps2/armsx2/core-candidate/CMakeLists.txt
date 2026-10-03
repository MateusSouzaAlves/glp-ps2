cmake_minimum_required(VERSION 3.22)
project(EmuOrbitARMSX2Candidate LANGUAGES C CXX ASM)

# Private feasibility recipe, deliberately not added to the product Gradle build.
# Reuse the pinned Android platform/core modules, not native-lib.cpp or its UI.
if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This candidate only supports Android arm64-v8a.")
endif()
if(NOT ARMSX2_SOURCE_DIR OR NOT ARMSX2_DEPENDENCY_DIR)
    message(FATAL_ERROR "Explicit verified source and dependency caches required.")
endif()
get_filename_component(ARMSX2_SOURCE_DIR "${ARMSX2_SOURCE_DIR}" REALPATH)
get_filename_component(ARMSX2_DEPENDENCY_DIR "${ARMSX2_DEPENDENCY_DIR}" REALPATH)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../../../config/ps2-armsx2-candidate.json" candidate)
string(JSON expected_revision GET "${candidate}" revision)
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${ARMSX2_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE source_revision OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT source_revision STREQUAL expected_revision)
    message(FATAL_ERROR "ARMSX2 candidate source revision mismatch.")
endif()
set(ARMSX2_RESOURCE_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/../../../../config/ps2-armsx2-resources.json")
file(SHA256 "${ARMSX2_RESOURCE_MANIFEST}" resource_manifest_hash)
set(ARMSX2_RUNTIME_DIR "${CMAKE_BINARY_DIR}/armsx2-runtime/${resource_manifest_hash}/system")
include("${CMAKE_CURRENT_LIST_DIR}/stage-resources.cmake")

set(ARMSX2_ANDROID_DIR "${ARMSX2_SOURCE_DIR}/platforms/android/app/src/main/cpp")
set(CMAKE_MODULE_PATH "${ARMSX2_ANDROID_DIR}/cmake")
set(TOP_CMAKE_WAS_SOURCED TRUE)
set(ENABLE_QT_UI OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBRETRO ON CACHE BOOL "" FORCE)
set(ENABLE_RECOMPILER_TEST_HOOKS OFF CACHE BOOL "" FORCE)
set(USE_OPENGL ON CACHE BOOL "" FORCE)
set(USE_VULKAN ON CACHE BOOL "" FORCE)
set(ARMSX2_USE_BUNDLED_SHADERC ON CACHE BOOL "" FORCE)
set(ARMSX2_USE_OBOE OFF CACHE BOOL "" FORCE)
set(ARMSX2_USE_ADRENOTOOLS OFF CACHE BOOL "" FORCE)
set(ARMSX2_ENABLE_LSFG OFF CACHE BOOL "" FORCE)
set(ARMSX2_HAVE_LIBRASHADER OFF)
set(ARMSX2_HAVE_DISCORD OFF)
set(ARMSX2_EMBED_RESOURCES OFF CACHE BOOL "" FORCE)
set(ARMSX2_EMBED_PATCHES OFF CACHE BOOL "" FORCE)
set(BUNDLE_EMOJI_FONT OFF CACHE BOOL "" FORCE)
set(LTO_PCSX2_CORE OFF CACHE BOOL "" FORCE)
set(POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(Pcsx2Utils)
check_no_parenthesis_in_path()
detect_operating_system()
detect_compiler()
include(BuildParameters)
# The Android frontend's BuildParameters predates the Libretro switch. The
# desktop module appends this to PCSX2_DEFS; supply the same contract explicitly.
list(APPEND PCSX2_DEFS ENABLE_LIBRETRO)
include("${CMAKE_CURRENT_LIST_DIR}/relocate-dependencies.cmake")

# get_git_version_info() would inspect this wrapper's repository, not the core.
set(PCSX2_GIT_HASH "${source_revision}")
string(JSON PCSX2_GIT_TAG GET "${candidate}" release)
set(PCSX2_GIT_REV "${PCSX2_GIT_TAG}")
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${ARMSX2_SOURCE_DIR}" log -1 --format=%cI
    OUTPUT_VARIABLE PCSX2_GIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
write_svnrev_h()

add_subdirectory("${ARMSX2_SOURCE_DIR}/common" "${CMAKE_BINARY_DIR}/common")
add_subdirectory("${ARMSX2_SOURCE_DIR}/3rdparty/libretro" "${CMAKE_BINARY_DIR}/libretro-headers")
add_subdirectory("${ARMSX2_SOURCE_DIR}/pcsx2" "${CMAKE_BINARY_DIR}/pcsx2")
include("${CMAKE_CURRENT_LIST_DIR}/disabled-input-source.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/state-writer-source.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/mobile-osd-source.cmake")

# Same upstream Libretro entry, with explicit roots and Android symbol isolation.
# Its own CMakeLists uses CMAKE_SOURCE_DIR (the desktop monorepo entry).
include("${CMAKE_CURRENT_LIST_DIR}/runtime-path-source.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/audio-output-source.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/lifecycle-source.cmake")
add_library(armsx2_candidate SHARED "${ARMSX2_LIBRETRO_MAIN}")
set_target_properties(armsx2_candidate PROPERTIES
    OUTPUT_NAME armsx2_libretro PREFIX "" POSITION_INDEPENDENT_CODE ON)
target_include_directories(armsx2_candidate PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../host"
    "${ARMSX2_SOURCE_DIR}" "${ARMSX2_SOURCE_DIR}/pcsx2"
    "${CMAKE_BINARY_DIR}/common/include")
target_link_libraries(armsx2_candidate PRIVATE
    PCSX2_FLAGS PCSX2 libretro-headers shaderc android log EGL GLESv3)
target_link_options(armsx2_candidate PRIVATE
    "LINKER:--no-undefined"
    "LINKER:--version-script=${ARMSX2_SOURCE_DIR}/pcsx2-libretro/link.T"
    "LINKER:-Map,${CMAKE_BINARY_DIR}/armsx2_libretro.map")
set_target_properties(armsx2_candidate PROPERTIES
    LINK_DEPENDS "${ARMSX2_SOURCE_DIR}/pcsx2-libretro/link.T")

# ADB-shell ABI proof only, never linked/packaged into the product or core.
add_executable(armsx2_abi_probe EXCLUDE_FROM_ALL abi_probe.c)
set_target_properties(armsx2_abi_probe PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(armsx2_abi_probe PRIVATE libretro-headers dl)
target_compile_definitions(armsx2_abi_probe PRIVATE _GNU_SOURCE)
target_compile_options(armsx2_abi_probe PRIVATE -Wall -Wextra -Werror)
target_include_directories(armsx2_abi_probe PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../host")

add_executable(armsx2_cpu_reply_probe EXCLUDE_FROM_ALL ../host/cpu_reply_probe.cpp)
target_compile_features(armsx2_cpu_reply_probe PRIVATE cxx_std_17)
# This non-polymorphic helper needs no RTTI. UBSan checks arithmetic, alignment,
# null, bounds etc.; vptr is not supported by this probe configuration. The
# standalone diagnostic retains the toolchain's make_shared vptr failure.
# Enable exceptions only for this probe's injected bad_alloc test; the core
# continues to compile with upstream -fno-exceptions.
target_compile_options(armsx2_cpu_reply_probe PRIVATE -Wall -Wextra -Werror -fno-rtti -fexceptions -fsanitize=undefined -fno-sanitize-recover=all)
target_link_options(armsx2_cpu_reply_probe PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)

add_executable(armsx2_stdlib_rtti_diagnostic EXCLUDE_FROM_ALL ../host/stdlib_rtti_diagnostic.cpp)
target_compile_features(armsx2_stdlib_rtti_diagnostic PRIVATE cxx_std_17)
target_compile_options(armsx2_stdlib_rtti_diagnostic PRIVATE -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all)
target_link_options(armsx2_stdlib_rtti_diagnostic PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)

add_executable(armsx2_pcm_probe EXCLUDE_FROM_ALL ../host/pcm_mailbox_probe.cpp)
target_compile_features(armsx2_pcm_probe PRIVATE cxx_std_17)
target_compile_options(armsx2_pcm_probe PRIVATE -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all)
target_link_options(armsx2_pcm_probe PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)

add_executable(armsx2_audio_peak_probe EXCLUDE_FROM_ALL ../host/audio_peak_guard_probe.cpp)
target_compile_features(armsx2_audio_peak_probe PRIVATE cxx_std_17)
target_compile_options(armsx2_audio_peak_probe PRIVATE -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all)
target_link_options(armsx2_audio_peak_probe PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)

add_executable(armsx2_input_probe EXCLUDE_FROM_ALL ../host/input_snapshot_probe.cpp)
target_link_libraries(armsx2_input_probe PRIVATE libretro-headers)
target_compile_features(armsx2_input_probe PRIVATE cxx_std_17)
target_compile_options(armsx2_input_probe PRIVATE -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all)
target_link_options(armsx2_input_probe PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)

# Private ImageReader/JNI BIOS proof, not a product dependency. The 3DS source
# is compiled in a separate DSO/process with no changes to its production host.
get_filename_component(EMUORBIT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/../host/surface-rebind-source.cmake")
# The product host shares the audited frontend but contains NO diagnostic
# entry point/ImageReader/audio proof. Static STL + hidden exports isolate it
# from the other consoles' differently pinned libc++ runtimes.
add_library(armsx2_product_host SHARED EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/../host/native_session.cpp"
    "${ARMSX2_PRIVATE_HOST_SOURCE}")
set_target_properties(armsx2_product_host PROPERTIES OUTPUT_NAME emuorbit_armsx2)
target_include_directories(armsx2_product_host PRIVATE
    "${ARMSX2_PRIVATE_HOST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../host"
    "${EMUORBIT_ROOT}/nintendo3dscore/src/main/cpp"
    "${ARMSX2_ANDROID_DIR}/3rdparty/vulkan/include")
target_link_libraries(armsx2_product_host PRIVATE libretro-headers android log dl)
target_compile_features(armsx2_product_host PRIVATE cxx_std_17)
target_compile_options(armsx2_product_host PRIVATE -Wall -Wextra -Werror -fvisibility=hidden -fexceptions)
target_link_options(armsx2_product_host PRIVATE "LINKER:--no-undefined"
    "LINKER:--version-script=${CMAKE_CURRENT_LIST_DIR}/../host/product.map")
set_target_properties(armsx2_product_host PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../host/product.map")
add_library(armsx2_boot_probe SHARED EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/../bootstrap-proof/boot_probe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../host/native_session.cpp"
    "${ARMSX2_PRIVATE_HOST_SOURCE}")
target_include_directories(armsx2_boot_probe PRIVATE
    "${ARMSX2_PRIVATE_HOST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../host"
    "${EMUORBIT_ROOT}/nintendo3dscore/src/main/cpp"
    "${ARMSX2_ANDROID_DIR}/3rdparty/vulkan/include")
target_link_libraries(armsx2_boot_probe PRIVATE libretro-headers android log dl)
target_compile_features(armsx2_boot_probe PRIVATE cxx_std_17)
target_compile_options(armsx2_boot_probe PRIVATE -Wall -Wextra -Werror -fvisibility=hidden -fexceptions)
target_link_options(armsx2_boot_probe PRIVATE "LINKER:--no-undefined")

# Resources remain external for this proof; a host must install the exact shader,
# GameIndex and font inventory before boot. Do not download latest patches.
foreach(forbidden emucore_4k emucore_16k flurry savers adrenotools oboe
        armsx2_discord discord_partner_sdk librashader pcsx2-qt)
    if(TARGET "${forbidden}")
        message(FATAL_ERROR "Unwanted frontend dependency: ${forbidden}")
    endif()
endforeach()
message(STATUS "Private ARMSX2 candidate: ${source_revision}; no product replacement or runtime certification")
