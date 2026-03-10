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

set(xxhash_header "${PATCH_ROOT}/xxhash.h")
if(EXISTS "${xxhash_header}")
  file(READ "${xxhash_header}" header_content)
  set(header_updated "${header_content}")

  string(REPLACE [=[#elif defined(_MSC_VER)
#  include <intrin.h>
#endif
]=]
                 [=[#elif defined(_MSC_VER)
#  if defined(__clang__) && (defined(_M_ARM64) || defined(_M_ARM64EC))
#    include <arm_neon.h>
#  elif defined(_M_ARM64) || defined(_M_ARM64EC)
#    include <arm64_neon.h>
#  endif
#  include <intrin.h>
#endif
]=]
                 header_updated
                 "${header_updated}")

  if(NOT header_updated STREQUAL header_content)
    file(WRITE "${xxhash_header}" "${header_updated}")
  endif()
endif()
