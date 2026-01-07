if(NOT DEFINED PATCH_ROOT)
  set(PATCH_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(patch_file "${PATCH_ROOT}/CMakeLists.txt")
if(NOT EXISTS "${patch_file}")
  message(FATAL_ERROR "streamvbyte patch: missing ${patch_file}")
endif()

file(READ "${patch_file}" content)
string(REGEX MATCH "cmake_minimum_required\\(VERSION[ ]+([0-9]+(\\.[0-9]+)*)\\)" match "${content}")
if(NOT match)
  message(FATAL_ERROR "streamvbyte patch: no cmake_minimum_required in ${patch_file}")
endif()

set(found_version "${CMAKE_MATCH_1}")
if(found_version VERSION_LESS "3.5")
  string(
    REGEX REPLACE "cmake_minimum_required\\(VERSION[ ]+[0-9.]+\\)"
    "cmake_minimum_required(VERSION 3.5)" updated "${content}"
  )
  if(NOT updated STREQUAL content)
    file(WRITE "${patch_file}" "${updated}")
  endif()
endif()
