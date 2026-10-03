# Mechanical relocation of the pinned Android dependency recipe. Keep upstream
# checkouts read-only and fail closed if the expected recipe shape changes.
file(READ "${ARMSX2_ANDROID_DIR}/cmake/SearchForStuff.cmake" deps_recipe)
set(oboe_line "\tadd_subdirectory(3rdparty/oboe EXCLUDE_FROM_ALL)")
string(FIND "${deps_recipe}" "${oboe_line}" oboe_pos)
if(oboe_pos EQUAL -1)
    message(FATAL_ERROR "Unexpected Android Oboe recipe; re-audit source.")
endif()
string(REPLACE "${oboe_line}" "# Oboe omitted: Libretro pulls PCM via frontend." deps_recipe "${deps_recipe}")
string(REPLACE "\${CMAKE_SOURCE_DIR}/3rdparty" "${ARMSX2_ANDROID_DIR}/3rdparty" deps_recipe "${deps_recipe}")
string(REPLACE "${ARMSX2_ANDROID_DIR}/3rdparty/shaderc/third_party" "${ARMSX2_DEPENDENCY_DIR}" deps_recipe "${deps_recipe}")
string(REGEX REPLACE "add_subdirectory\\(3rdparty/([A-Za-z0-9_./-]+) EXCLUDE_FROM_ALL\\)"
    "add_subdirectory(\"${ARMSX2_ANDROID_DIR}/3rdparty/\\1\" \"${CMAKE_BINARY_DIR}/3rdparty/\\1\" EXCLUDE_FROM_ALL)"
    deps_recipe "${deps_recipe}")
if(deps_recipe MATCHES "add_subdirectory\\(3rdparty/" OR deps_recipe MATCHES "\\$\\{CMAKE_SOURCE_DIR\\}/3rdparty")
    message(FATAL_ERROR "Unrelocated dependency path; re-audit source.")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/CandidateSearchForStuff.cmake" "${deps_recipe}")
include("${CMAKE_BINARY_DIR}/CandidateSearchForStuff.cmake")
