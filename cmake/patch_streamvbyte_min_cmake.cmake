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
set(updated_content "${content}")
if(found_version VERSION_LESS "3.5")
  string(
    REGEX REPLACE "cmake_minimum_required\\(VERSION[ ]+[0-9.]+\\)"
    "cmake_minimum_required(VERSION 3.5)" updated_content "${content}"
  )
endif()

string(
  REPLACE "if(CMAKE_SYSTEM_PROCESSOR MATCHES \"^(aarch64.*|AARCH64.*)\")"
          "if(CMAKE_SYSTEM_PROCESSOR MATCHES \"^(aarch64.*|AARCH64.*|arm64.*|ARM64.*)\")"
          updated_arch "${updated_content}"
)
if(NOT updated_arch STREQUAL content)
  file(WRITE "${patch_file}" "${updated_arch}")
endif()

set(isa_file "${PATCH_ROOT}/src/streamvbyte_isadetection.h")
if(EXISTS "${isa_file}")
  file(READ "${isa_file}" isa_content)

  string(
    REPLACE "#if defined(_MSC_VER)"
            "#if defined(_MSC_VER) && (defined(_M_AMD64) || defined(_M_IX86))"
            isa_updated "${isa_content}"
  )
  string(
    REPLACE "#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))"
            "#elif defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))\n#include <arm64_neon.h>\n#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))"
            isa_updated "${isa_updated}"
  )

  if(NOT isa_updated STREQUAL isa_content)
    file(WRITE "${isa_file}" "${isa_updated}")
  endif()
endif()
