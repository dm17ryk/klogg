if(NOT DEFINED PATCH_ROOT)
  set(PATCH_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(patch_file "${PATCH_ROOT}/cmake_unofficial/CMakeLists.txt")
if(NOT EXISTS "${patch_file}")
  message(FATAL_ERROR "xxHash patch: missing ${patch_file}")
endif()

file(READ "${patch_file}" content)
string(
  REGEX REPLACE "cmake_minimum_required[ \t]*\\([^\\)]*\\)[ \t]*[\\r\\n]*"
  ""
  stripped
  "${content}"
)
file(WRITE "${patch_file}" "cmake_minimum_required(VERSION 3.10)\n\n${stripped}")
