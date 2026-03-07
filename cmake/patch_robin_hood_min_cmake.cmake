if(NOT DEFINED PATCH_ROOT)
  set(PATCH_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(patch_file "${PATCH_ROOT}/CMakeLists.txt")
if(NOT EXISTS "${patch_file}")
  message(FATAL_ERROR "robin_hood patch: missing ${patch_file}")
endif()

file(READ "${patch_file}" content)
string(REGEX MATCH "cmake_minimum_required\\(VERSION[ ]+([0-9]+(\\.[0-9]+)*)\\)" match "${content}")
if(NOT match)
  message(FATAL_ERROR "robin_hood patch: no cmake_minimum_required in ${patch_file}")
endif()

set(found_version "${CMAKE_MATCH_1}")
set(updated_content "${content}")
if(found_version VERSION_LESS "3.10")
  string(
    REGEX REPLACE "cmake_minimum_required\\(VERSION[ ]+[0-9.]+\\)"
    "cmake_minimum_required(VERSION 3.10)" updated_content "${content}"
  )
endif()

if(NOT updated_content STREQUAL content)
  file(WRITE "${patch_file}" "${updated_content}")
endif()

set(header_file "${PATCH_ROOT}/src/include/robin_hood.h")
if(EXISTS "${header_file}")
  file(READ "${header_file}" header_content)
  if(NOT header_content MATCHES "#include <cstdint>")
    string(REPLACE "#include <cstddef>"
                   "#include <cstddef>\n#include <cstdint>"
                   header_updated "${header_content}")
    if(NOT header_updated STREQUAL header_content)
      file(WRITE "${header_file}" "${header_updated}")
    endif()
  endif()
endif()
