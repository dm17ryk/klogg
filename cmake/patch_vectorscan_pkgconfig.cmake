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

set(_klogg_vs_arch_flags
"if (NOT FAT_RUNTIME)
    if (GNUCC_TUNE)
        set(ARCH_C_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}\")
        set(ARCH_CXX_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}\")
    else()
        set(ARCH_C_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_C_FLAGS}\")
        set(ARCH_CXX_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_CXX_FLAGS}\")
    endif()
endif()")
set(_klogg_vs_arch_flags_replacement
"if (WIN32 AND MSVC AND ARCH_AARCH64)
    set(ARCH_C_FLAGS \"\")
    set(ARCH_CXX_FLAGS \"\")
elseif (NOT FAT_RUNTIME)
    if (GNUCC_TUNE)
        set(ARCH_C_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}\")
        set(ARCH_CXX_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -${TUNE_FLAG}=${GNUCC_TUNE}\")
    else()
        set(ARCH_C_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_C_FLAGS}\")
        set(ARCH_CXX_FLAGS \"-${ARCH_FLAG}=${GNUCC_ARCH} -mtune=${TUNE_FLAG} ${ARCH_CXX_FLAGS}\")
    endif()
endif()")
string(REPLACE "${_klogg_vs_arch_flags}" "${_klogg_vs_arch_flags_replacement}" updated "${updated}")

set(_klogg_vs_opt_flags
"if(RELEASE_BUILD)
    if (NOT CMAKE_BUILD_TYPE MATCHES MINSIZEREL)
        set(OPT_C_FLAG \"-O3\")
        set(OPT_CXX_FLAG \"-O3\")
    else ()
        set(OPT_C_FLAG \"-Os\")
        set(OPT_CXX_FLAG \"-Os\")
    endif ()
else()
    set(OPT_C_FLAG \"-O0\")
    set(OPT_CXX_FLAG \"-O0\")
endif(RELEASE_BUILD)")
set(_klogg_vs_opt_flags_replacement
"if(WIN32 AND MSVC AND ARCH_AARCH64)
    set(OPT_C_FLAG \"\")
    set(OPT_CXX_FLAG \"\")
elseif(RELEASE_BUILD)
    if (NOT CMAKE_BUILD_TYPE MATCHES MINSIZEREL)
        set(OPT_C_FLAG \"-O3\")
        set(OPT_CXX_FLAG \"-O3\")
    else ()
        set(OPT_C_FLAG \"-Os\")
        set(OPT_CXX_FLAG \"-Os\")
    endif ()
else()
    set(OPT_C_FLAG \"-O0\")
    set(OPT_CXX_FLAG \"-O0\")
endif(RELEASE_BUILD)")
string(REPLACE "${_klogg_vs_opt_flags}" "${_klogg_vs_opt_flags_replacement}" updated "${updated}")

string(REPLACE
  "include (${CMAKE_MODULE_PATH}/cflags-generic.cmake)"
  "if(WIN32 AND MSVC AND ARCH_AARCH64)\n  set(EXTRA_C_FLAGS \"\")\n  set(EXTRA_CXX_FLAGS \"\")\nelse()\n  include (${CMAKE_MODULE_PATH}/cflags-generic.cmake)\nendif()"
  updated
  "${updated}")

if(NOT updated STREQUAL content)
  file(WRITE "${patch_file}" "${updated}")
endif()
