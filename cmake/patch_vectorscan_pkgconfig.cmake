if(NOT DEFINED PATCH_ROOT)
  set(PATCH_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(patch_file "${PATCH_ROOT}/CMakeLists.txt")
if(NOT EXISTS "${patch_file}")
  message(FATAL_ERROR "vectorscan patch: missing ${patch_file}")
endif()

file(READ "${patch_file}" content)
set(updated "${content}")

string(REPLACE "find_package(PkgConfig REQUIRED)" "find_package(PkgConfig QUIET)" updated "${updated}")

if(NOT updated STREQUAL content)
  file(WRITE "${patch_file}" "${updated}")
endif()
