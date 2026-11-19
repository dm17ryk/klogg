set(QAPPLICATION_CLASS QApplication)

include(CPM)

set(_TMP_CPM_USE_LOCAL_PACKAGES ${CPM_USE_LOCAL_PACKAGES})

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

cpmaddpackage("gh:simdutf/simdutf@5.6.2")
cpmaddpackage(
  NAME
  type_safe
  GITHUB_REPOSITORY
  foonathan/type_safe
  VERSION
  0.2.4
  PATCH_COMMAND
  ${CMAKE_COMMAND}
  -DPATCH_ROOT=.
  -P
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_type_safe_debug_assert.cmake
)
cpmaddpackage("gh:RoaringBitmap/CRoaring@4.2.1")
cpmaddpackage(
  NAME
  streamvbyte
  GITHUB_REPOSITORY
  lemire/streamvbyte
  VERSION
  1.0.0
  PATCH_COMMAND
  ${CMAKE_COMMAND}
  -DPATCH_ROOT=.
  -P
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_streamvbyte_min_cmake.cmake
)

if(TARGET streamvbyte AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64.*|AARCH64.*|arm64.*|ARM64.*)$")
  target_compile_definitions(streamvbyte PRIVATE __ARM_NEON__)
endif()

if(APPLE)
  cpmaddpackage(
    NAME
    macdeployqtfix
    GITHUB_REPOSITORY
    arl/macdeployqtfix
    GIT_TAG
    df888505849d3c06d20a4338af276dfa7d11826a
    DOWNLOAD_ONLY
    YES
  )
  if(macdeployqtfix_ADDED)
    configure_file(${macdeployqtfix_SOURCE_DIR}/macdeployqtfix.py ${CMAKE_BINARY_DIR}/macdeployqtfix.py COPYONLY)
  endif()
endif()

cpmaddpackage(
  NAME
  maddy
  GITHUB_REPOSITORY
  variar/maddy
  GIT_TAG
  602e26613e624535e2de883b3f2c98a16729d1d4
  EXCLUDE_FROM_ALL
  YES
)

if(KLOGG_REGEX_BACKEND STREQUAL "hyperscan")
  set(_klogg_hs_options
      "BUILD_SHARED_LIBS OFF"
      "BUILD_STATIC_AND_SHARED OFF"
      "BUILD_EXAMPLES OFF")

  set(_klogg_hs_fat_runtime OFF)
  set(_klogg_hs_build_avx512 OFF)
  set(_klogg_hs_build_avx512vbmi OFF)

  if(KLOGG_TARGET_OS STREQUAL "linux" AND KLOGG_TARGET_IS_X64)
    set(_klogg_hs_fat_runtime ON)

    check_c_compiler_flag("-march=skylake-avx512" KLOGG_HS_C_SKYLAKE_AVX512)
    check_cxx_compiler_flag("-march=skylake-avx512" KLOGG_HS_CXX_SKYLAKE_AVX512)
    if(KLOGG_HS_C_SKYLAKE_AVX512 AND KLOGG_HS_CXX_SKYLAKE_AVX512)
      set(_klogg_hs_build_avx512 ON)
    endif()

    check_c_compiler_flag("-march=icelake-server" KLOGG_HS_C_ICELAKE_SERVER)
    check_cxx_compiler_flag("-march=icelake-server" KLOGG_HS_CXX_ICELAKE_SERVER)
    if(KLOGG_HS_C_ICELAKE_SERVER AND KLOGG_HS_CXX_ICELAKE_SERVER)
      set(_klogg_hs_build_avx512vbmi ON)
    endif()
  endif()

  list(APPEND _klogg_hs_options
       "FAT_RUNTIME ${_klogg_hs_fat_runtime}"
       "BUILD_AVX512 ${_klogg_hs_build_avx512}"
       "BUILD_AVX512VBMI ${_klogg_hs_build_avx512vbmi}")

  message(
    STATUS
      "Configuring Hyperscan backend: fat-runtime=${_klogg_hs_fat_runtime}, avx512=${_klogg_hs_build_avx512}, avx512vbmi=${_klogg_hs_build_avx512vbmi}"
  )

  cpmaddpackage(
    NAME
    hyperscan
    GITHUB_REPOSITORY
    dm17ryk/hyperscan
    GIT_TAG
    d458377e75546f3eb081d13f1a9b8f04f4493cfa
    EXCLUDE_FROM_ALL
    YES
    OPTIONS
    ${_klogg_hs_options}
  )
  if(MSVC)
    foreach(_hs_target hs hs_compile hs_exec)
      if(TARGET ${_hs_target})
        foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
          string(TOUPPER ${_cfg} _cfg_upper)
          set_target_properties(
            ${_hs_target}
            PROPERTIES
              ARCHIVE_OUTPUT_DIRECTORY_${_cfg_upper}
              "${hyperscan_BINARY_DIR}/lib/${_cfg}"
              LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper}
              "${hyperscan_BINARY_DIR}/bin/${_cfg}"
              RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper}
              "${hyperscan_BINARY_DIR}/bin/${_cfg}")
        endforeach()
      endif()
    endforeach()
  endif()
  message("Adding alias for hyperscan")
  add_library(klogg_hyperscan INTERFACE)
  target_link_libraries(klogg_hyperscan INTERFACE hs)
  target_include_directories(klogg_hyperscan INTERFACE ${hyperscan_SOURCE_DIR}/src)
elseif(KLOGG_REGEX_BACKEND STREQUAL "vectorscan")
  set(_klogg_vs_fat_runtime OFF)
  set(_klogg_vs_build_avx2 OFF)
  set(_klogg_vs_build_avx512 OFF)
  set(_klogg_vs_build_avx512vbmi OFF)
  set(_klogg_vs_build_sve OFF)
  set(_klogg_vs_build_sve2 OFF)
  set(_klogg_vs_build_sve2_bitperm OFF)
  set(_klogg_vs_target_label "${KLOGG_TARGET_OS}-${KLOGG_TARGET_ARCH}")

  if(KLOGG_TARGET_OS STREQUAL "linux" AND KLOGG_TARGET_IS_X64)
    check_c_compiler_flag("-march=skylake-avx512" KLOGG_VS_C_SKYLAKE_AVX512)
    check_cxx_compiler_flag("-march=skylake-avx512" KLOGG_VS_CXX_SKYLAKE_AVX512)
    check_c_compiler_flag("-march=icelake-server" KLOGG_VS_C_ICELAKE_SERVER)
    check_cxx_compiler_flag("-march=icelake-server" KLOGG_VS_CXX_ICELAKE_SERVER)

    set(_klogg_vs_fat_runtime ON)
    set(_klogg_vs_build_avx2 ON)

    if(KLOGG_VS_C_SKYLAKE_AVX512 AND KLOGG_VS_CXX_SKYLAKE_AVX512)
      set(_klogg_vs_build_avx512 ON)
    endif()

    if(KLOGG_VS_C_ICELAKE_SERVER AND KLOGG_VS_CXX_ICELAKE_SERVER)
      set(_klogg_vs_build_avx512vbmi ON)
    endif()
  elseif(KLOGG_TARGET_OS STREQUAL "linux" AND KLOGG_TARGET_IS_ARM64)
    # Vectorscan detects unsupported Arm extensions and falls back to the
    # strongest supported runtime variant.
    set(_klogg_vs_fat_runtime ON)
    set(_klogg_vs_build_sve ON)
    set(_klogg_vs_build_sve2 ON)
    set(_klogg_vs_build_sve2_bitperm ON)
  elseif(KLOGG_TARGET_OS STREQUAL "windows" AND KLOGG_TARGET_IS_ARM64)
    # Windows arm64 stays on a distributable native Vectorscan configuration.
    # Keep the runtime non-fat and avoid x86-only SIMD feature variants.
    set(_klogg_vs_target_label "windows-arm64-native")
  endif()

  set(_klogg_vectorscan_options
      "BUILD_STATIC_LIBS ON"
      "BUILD_UNIT OFF"
      "BUILD_TOOLS OFF"
      "BUILD_EXAMPLES OFF"
      "BUILD_BENCHMARKS OFF"
      "BUILD_DOC OFF"
      "BUILD_CHIMERA OFF"
      "USE_CPU_NATIVE OFF"
      "BUILD_AVX2 ${_klogg_vs_build_avx2}"
      "BUILD_AVX512 ${_klogg_vs_build_avx512}"
      "BUILD_AVX512VBMI ${_klogg_vs_build_avx512vbmi}"
      "BUILD_SVE ${_klogg_vs_build_sve}"
      "BUILD_SVE2 ${_klogg_vs_build_sve2}"
      "BUILD_SVE2_BITPERM ${_klogg_vs_build_sve2_bitperm}"
      "FAT_RUNTIME ${_klogg_vs_fat_runtime}")

  message(
    STATUS
      "Configuring Vectorscan backend for ${_klogg_vs_target_label}: fat-runtime=${_klogg_vs_fat_runtime}, avx2=${_klogg_vs_build_avx2}, avx512=${_klogg_vs_build_avx512}, avx512vbmi=${_klogg_vs_build_avx512vbmi}, sve=${_klogg_vs_build_sve}, sve2=${_klogg_vs_build_sve2}, sve2_bitperm=${_klogg_vs_build_sve2_bitperm}"
  )

  cpmaddpackage(
    NAME
    vectorscan
    GITHUB_REPOSITORY
    VectorCamp/vectorscan
    GIT_TAG
    d29730e1cb9daaa66bda63426cdce83505d2c809
    PATCH_COMMAND
    ${CMAKE_COMMAND}
    -DPATCH_ROOT=.
    -P
    ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_vectorscan_pkgconfig.cmake
    EXCLUDE_FROM_ALL
    YES
    OPTIONS
    ${_klogg_vectorscan_options}
  )

  message("Adding alias for vectorscan")
  add_library(klogg_vectorscan INTERFACE)
  target_link_libraries(klogg_vectorscan INTERFACE hs)
  target_include_directories(klogg_vectorscan INTERFACE
    ${vectorscan_SOURCE_DIR}/src
    ${vectorscan_BINARY_DIR}
  )
endif()

cpmaddpackage(
  NAME
  Uchardet
  GIT_REPOSITORY
  https://gitlab.freedesktop.org/uchardet/uchardet
  VERSION
  0.0.8
  PATCH_COMMAND
  ${CMAKE_COMMAND}
  -DPATCH_ROOT=.
  -P
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_uchardet_min_cmake.cmake
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_BINARY OFF"
)
if(Uchardet_ADDED)
  message("Adding alias for uchardet")
  add_library(klogg_uchardet_wrapper INTERFACE)
  target_link_libraries(klogg_uchardet_wrapper INTERFACE libuchardet)
  target_include_directories(klogg_uchardet_wrapper INTERFACE ${Uchardet_SOURCE_DIR}/src)
else()
  add_library(klogg_uchardet_wrapper INTERFACE)
  target_link_libraries(klogg_uchardet_wrapper INTERFACE ${UCHARDET_LIBRARY})
  target_include_directories(klogg_uchardet_wrapper INTERFACE ${UCHARDET_INCLUDE_DIR})
endif()

if(${QT_VERSION_MAJOR} EQUAL 6) 
set(CPM_USE_LOCAL_PACKAGES OFF)
endif()

cpmaddpackage(
  NAME
  KF5Archive
  GITHUB_REPOSITORY
  dm17ryk/klogg_karchive
  GIT_TAG
  f546bf6ae66a8d34b43da5a41afcfbf4e1a47906
  UPDATE_COMMAND
  ""
  PATCH_COMMAND
  ${CMAKE_COMMAND}
  -DPATCH_ROOT=.
  -P
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_karchive_qt6_arg.cmake
  EXCLUDE_FROM_ALL
  YES
)
if(NOT KF5Archive_ADDED)
  find_package(KF5Archive)
  add_library(klogg_karchive INTERFACE)
  target_link_libraries(klogg_karchive INTERFACE KF5::Archive)
endif()

if(${QT_VERSION_MAJOR} EQUAL 6)
message("Resetting CPM_USE_LOCAL_PACKAGES to ${_TMP_CPM_USE_LOCAL_PACKAGES}")
set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})
endif()

cpmaddpackage(
  NAME
  robin_hood
  GITHUB_REPOSITORY
  martinus/robin-hood-hashing
  GIT_TAG
  3.11.2
  PATCH_COMMAND
  ${CMAKE_COMMAND}
  -DPATCH_ROOT=.
  -P
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_robin_hood_min_cmake.cmake
  EXCLUDE_FROM_ALL
  YES
)
if(NOT TARGET robin_hood)
  message("Adding imported target for robin_hood")
  add_library(robin_hood INTERFACE)
  target_include_directories(robin_hood INTERFACE ${ROBIN_HOOD_INCLUDE_DIRS})
endif(NOT TARGET robin_hood)

if(KLOGG_BUILD_TESTS)
  cpmaddpackage(
    NAME
    backward-cpp
    GITHUB_REPOSITORY
    bombela/backward-cpp
    VERSION
    1.6
    PATCH_COMMAND
    ${CMAKE_COMMAND}
    -DPATCH_ROOT=.
    -P
    ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_backward_cpp_min_cmake.cmake
  )
  cpmaddpackage(
    NAME
    Catch2
    GITHUB_REPOSITORY
    catchorg/Catch2
    VERSION
    2.13.8
    EXCLUDE_FROM_ALL
    YES
  )
  if(NOT TARGET Catch2)
    message("Adding imported target for catch2")
    add_library(Catch2 INTERFACE)
    target_include_directories(Catch2 INTERFACE ${CATCH2_INCLUDE_DIRS})
  endif(NOT TARGET Catch2)
endif()

cpmaddpackage(
  NAME
  KDSingleApplication
  GITHUB_REPOSITORY
  variar/KDSingleApplication
  GIT_TAG
  5b30db30266f92bc01f1439777803ce8dbf16c79
  DOWNLOAD_ONLY
  YES
)
if(KDSingleApplication_ADDED)
  add_library(
    kdsingleapp STATIC ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication.cpp
                     ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication_localsocket.cpp
  )

  target_include_directories(kdsingleapp PUBLIC ${KDSingleApplication_SOURCE_DIR}/src)
  target_link_libraries(kdsingleapp Qt${QT_VERSION_MAJOR}::Core)
  target_compile_definitions(kdsingleapp PUBLIC KDSINGLEAPPLICATION_STATIC_BUILD)

  set_target_properties(kdsingleapp PROPERTIES AUTOMOC ON)
endif()

# QtKeychain keeps personal access tokens of plugin repositories in the operating system
# credential store (docs/plugin-repositories.md 3.5). Built as our own static target: no tests,
# translations or install rules. On Linux it needs Qt DBus (KWallet and the Secret Service through
# libsecret, which it loads at run time); without Qt DBus CILogg keeps tokens for the session only.
cpmaddpackage(
  NAME
  qtkeychain
  GITHUB_REPOSITORY
  frankosterfeld/qtkeychain
  GIT_TAG
  0.17.0
  DOWNLOAD_ONLY
  YES
)
if(qtkeychain_ADDED)
  set(_qtk_dir ${qtkeychain_SOURCE_DIR}/qtkeychain)
  set(_qtk_sources ${_qtk_dir}/keychain.cpp ${_qtk_dir}/keychain.h ${_qtk_dir}/keychain_p.h)
  set(_qtk_usable OFF)
  if(WIN32)
    list(APPEND _qtk_sources ${_qtk_dir}/keychain_win.cpp)
    set(_qtk_usable ON)
  elseif(APPLE)
    list(APPEND _qtk_sources ${_qtk_dir}/keychain_apple.mm)
    set(_qtk_usable ON)
  elseif(UNIX)
    find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS DBus)
    if(TARGET Qt${QT_VERSION_MAJOR}::DBus)
      list(APPEND _qtk_sources
        ${_qtk_dir}/keychain_unix.cpp ${_qtk_dir}/gnomekeyring.cpp ${_qtk_dir}/gnomekeyring_p.h
        ${_qtk_dir}/libsecret.cpp ${_qtk_dir}/libsecret_p.h
        ${_qtk_dir}/plaintextstore.cpp ${_qtk_dir}/plaintextstore_p.h)
      qt_add_dbus_interface(_qtk_sources ${_qtk_dir}/org.kde.KWallet.xml kwallet_interface)
      set(_qtk_usable ON)
    else()
      message(STATUS "Qt DBus not found: plugin repository tokens are kept for the session only")
    endif()
  endif()

  if(_qtk_usable)
    set(_qtk_generated ${CMAKE_CURRENT_BINARY_DIR}/qtkeychain_generated)
    file(WRITE ${_qtk_generated}/qkeychain_export.h
         "#pragma once\n#define QKEYCHAIN_EXPORT\n#define QKEYCHAIN_NO_EXPORT\n")
    add_library(klogg_qtkeychain STATIC ${_qtk_sources})
    set_target_properties(klogg_qtkeychain PROPERTIES AUTOMOC ON)
    target_include_directories(klogg_qtkeychain
      PUBLIC ${qtkeychain_SOURCE_DIR} ${_qtk_generated}
      PRIVATE ${_qtk_dir} ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(klogg_qtkeychain PUBLIC Qt${QT_VERSION_MAJOR}::Core)
    target_compile_definitions(klogg_qtkeychain INTERFACE CILOGG_HAVE_QTKEYCHAIN=1)
    if(WIN32)
      target_compile_definitions(klogg_qtkeychain PRIVATE UNICODE _UNICODE USE_CREDENTIAL_STORE=1)
      target_link_libraries(klogg_qtkeychain PRIVATE crypt32 advapi32)
    elseif(APPLE)
      target_link_libraries(klogg_qtkeychain PRIVATE "-framework Foundation" "-framework Security")
    else()
      target_compile_definitions(klogg_qtkeychain PRIVATE KEYCHAIN_DBUS=1)
      target_link_libraries(klogg_qtkeychain PRIVATE Qt${QT_VERSION_MAJOR}::DBus)
      find_package(PkgConfig QUIET)
      if(PKG_CONFIG_FOUND)
        pkg_check_modules(QTK_LIBSECRET QUIET libsecret-1)
        pkg_check_modules(QTK_GLIB QUIET glib-2.0)
      endif()
      if(QTK_LIBSECRET_FOUND AND QTK_GLIB_FOUND)
        # Only headers and g_error_free: libsecret itself is loaded at run time.
        target_compile_definitions(klogg_qtkeychain PRIVATE HAVE_LIBSECRET=1)
        target_include_directories(klogg_qtkeychain SYSTEM PRIVATE ${QTK_LIBSECRET_INCLUDE_DIRS})
        target_link_libraries(klogg_qtkeychain PRIVATE ${QTK_GLIB_LINK_LIBRARIES})
      else()
        message(STATUS "libsecret headers not found: QtKeychain uses KWallet only")
      endif()
    endif()
  endif()
endif()


cpmaddpackage(
  NAME
  xxHash
  GITHUB_REPOSITORY
  Cyan4973/xxHash
  VERSION
  0.8.1
  PATCH_COMMAND
  ${CMAKE_COMMAND}
  -DPATCH_ROOT=.
  -P
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_xxhash_min_cmake.cmake
  DOWNLOAD_ONLY
  YES
)
if(xxHash_ADDED)
  set(XXHASH_BUILD_ENABLE_INLINE_API OFF)
  set(XXHASH_BUILD_XXHSUM OFF)
  add_subdirectory(${xxHash_SOURCE_DIR}/cmake_unofficial ${CMAKE_BINARY_DIR}/xxhash EXCLUDE_FROM_ALL)
endif()

cpmaddpackage(
  NAME
  whereami
  GITHUB_REPOSITORY
  gpakosz/whereami
  GIT_TAG
  dcb52a058dc14530ba9ae05e4339bd3ddfae0e0e
  DOWNLOAD_ONLY
  YES
)
if(whereami_ADDED)
  add_library(whereami STATIC ${whereami_SOURCE_DIR}/src/whereami.h ${whereami_SOURCE_DIR}/src/whereami.c)
  target_include_directories(whereami PUBLIC ${whereami_SOURCE_DIR}/src)
endif()

cpmaddpackage(
  NAME
  exprtk
  GITHUB_REPOSITORY
  dm17ryk/klogg_exprtk
  GIT_TAG
  1f9f4cd7d2620b7b24232de9ea22908d63913459
  DOWNLOAD_ONLY
  YES
)
if(exprtk_ADDED)
  add_library(exprtk INTERFACE)
  target_link_libraries(exprtk INTERFACE robin_hood)
  target_include_directories(exprtk SYSTEM INTERFACE ${exprtk_SOURCE_DIR})
  target_compile_definitions(
    exprtk
    INTERFACE -Dexprtk_disable_caseinsensitivity
              -Dexprtk_disable_comments
              -Dexprtk_disable_break_continue
              -Dexprtk_disable_return_statement
              -Dexprtk_disable_superscalar_unroll
              -Dexprtk_disable_rtl_io_file
              -Dexprtk_disable_rtl_vecops
              -Dexprtk_disable_string_capabilities
  )
endif()

cpmaddpackage(
  NAME
  KDToolBox
  GITHUB_REPOSITORY
  KDAB/KDToolBox
  GIT_TAG
  6468867d1a46eabe1bcb2cd342f338fe66f06675
  DOWNLOAD_ONLY
  YES
)
if(KDToolBox_ADDED)
  add_library(
    kdtoolbox STATIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.cpp
                     ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.h
  )

  target_include_directories(kdtoolbox PUBLIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src)
  target_link_libraries(kdtoolbox Qt${QT_VERSION_MAJOR}::Core)
  set_target_properties(kdtoolbox PROPERTIES AUTOMOC ON)
endif()

cpmaddpackage(
  NAME
  efsw
  GITHUB_REPOSITORY
  SpartanJ/efsw
  VERSION
  1.7.2
  GIT_TAG
  41ddf6822f2d0dec7e14fafa09c4cef391137b20
  PATCH_COMMAND
  ${CMAKE_COMMAND}
  -DPATCH_ROOT=.
  -P
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_efsw_windows_pending.cmake
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_TEST_APP OFF"
)
if(efsw_ADDED)
  target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)
endif()

if(WIN32)
  set(BUILD_SHARED_LIBS ON)
endif()
cpmaddpackage(
  NAME
  tbb
  GITHUB_REPOSITORY
  variar/oneTBB
  GIT_TAG
  c9be1ac2930f02dea523003ed801b4489f3e6b6e
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "TBB_TEST OFF"
  "TBB_EXAMPLES OFF"
  "TBB_STRICT OFF"
)

if(WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()

set(CPM_USE_LOCAL_PACKAGES OFF)
cpmaddpackage(
  NAME
  mimalloc
  GITHUB_REPOSITORY
  microsoft/mimalloc
  VERSION
  2.1.7
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "MI_BUILD_TESTS OFF"
  "MI_SECURE OFF"
  "MI_BUILD_SHARED OFF"
  "MI_BUILD_STATIC ON"
  "MI_BUILD_OBJECT OFF"
  "MI_OVERRIDE OFF"
)
if(mimalloc_ADDED)
  message("Adding alias for mimalloc")
  add_library(klogg_mimalloc_wrapper INTERFACE)
  target_link_libraries(klogg_mimalloc_wrapper INTERFACE mimalloc-static)
  target_include_directories(klogg_mimalloc_wrapper INTERFACE ${mimalloc_SOURCE_DIR}/include)
else()
  add_library(klogg_mimalloc_wrapper INTERFACE)
  target_link_libraries(klogg_mimalloc_wrapper INTERFACE ${MIMALLOC_LIBRARY})
  target_include_directories(klogg_mimalloc_wrapper INTERFACE ${MIMALLOC_INCLUDE_DIR})
endif()

set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})

if(KLOGG_USE_SENTRY)
  set(SENTRY_BACKEND
     "crashpad"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_TRANSPORT
     "none"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_BUILD_EXAMPLES
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL_DEV
     OFF
     CACHE INTERNAL "" FORCE
  )

  cpmaddpackage(
    NAME
    sentry
    GITHUB_REPOSITORY
    getsentry/sentry-native
    GIT_TAG
    a3d58622a807b9dda174cb9fc18fa0f98c89d043
    EXCLUDE_FROM_ALL
    YES
  )

  if(WIN32)
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler.exe")
  else()
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler")
  endif()
endif(KLOGG_USE_SENTRY)

cpmaddpackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  VERSION 3.4.0
  OPTIONS "TOMLPLUSPLUS_BUILD_TESTS OFF" "TOMLPLUSPLUS_BUILD_EXAMPLES OFF"
)

set(klogg_cpm_targets
    xxhash
    Catch2
    roaring
    roaring-headers-cpp
    libuchardet
    robin_hood
    whereami
    simdutf
    efsw
    SingleApplication
    hs
    tbb
    exprtk
    type_safe
    mimalloc-static
    sentry
    crashpad_client
    crashpad_compat
    crashpad_util
    mini_chromium
    streamvbyte
    FastPFor
)
foreach(target ${klogg_cpm_targets})
  if(TARGET ${target})
    get_property(
      ${target}_include_dirs
      TARGET ${target}
      PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    )
    #message("Marking ${${target}_include_dirs} as system incldue")
    set_property(TARGET ${target} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${${target}_include_dirs})
  endif()
endforeach()



if(KLOGG_ENABLE_TERMINAL OR KLOGG_BUILD_TESTS)
  add_subdirectory(libvterm)
endif()
