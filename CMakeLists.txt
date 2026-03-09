cmake_minimum_required(VERSION 3.14)

project(
  klogg
  VERSION 24.11.2
  DESCRIPTION "klogg log viewer"
  LANGUAGES C CXX ASM
)

set(PROJECT_HOMEPAGE_URL "https://github.com/dm17ryk/klogg")

string(TIMESTAMP PRODUCT_CURRENT_YEAR "%Y")
set(COMPANY "Dmitry Kokotov")
set(COPYRIGHT "Copyright (c) ${PRODUCT_CURRENT_YEAR} Nicolas Bonnefon, Anton Filimonov, Dmitry Kokotov and other contributors.")
set(IDENTIFIER "com.github.dm17ryk.klogg")
set(PROJECT_DESCRIPTION "${PROJECT_NAME} log viewer")

option(KLOGG_BUILD_TESTS "Build tests" ON)
option(KLOGG_USE_LTO "Use link time optimization" ON)
option(KLOGG_USE_SENTRY "Use Sentry" OFF)
option(KLOGG_GENERIC_CPU "Build for generic CPU" OFF)
option(KLOGG_BUILD_DOCUMENTATION "Build generated HTML documentation" ON)
option(KLOGG_OSX_DEPLOYMENT_TARGET "Override target MacOS version" "")

if(KLOGG_USE_LTO
   AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10
   AND KLOGG_BUILD_TESTS)
  message(WARNING "Disabling LTO for GCC < 10 when tests are enabled to avoid LTO ICE.")
  set(KLOGG_USE_LTO OFF)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(KLOGG_ARCH 32)
else()
  set(KLOGG_ARCH 64)
endif()

function(klogg_normalize_target_arch out_arch out_source)
  set(_klogg_candidates "")

  if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(APPEND _klogg_candidates ${CMAKE_OSX_ARCHITECTURES})
  endif()

  if(MSVC)
    if(CMAKE_VS_PLATFORM_NAME)
      list(APPEND _klogg_candidates "${CMAKE_VS_PLATFORM_NAME}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
      list(APPEND _klogg_candidates "${CMAKE_GENERATOR_PLATFORM}")
    endif()
  endif()

  if(CMAKE_SYSTEM_PROCESSOR)
    list(APPEND _klogg_candidates "${CMAKE_SYSTEM_PROCESSOR}")
  endif()

  foreach(_klogg_candidate IN LISTS _klogg_candidates)
    string(TOLOWER "${_klogg_candidate}" _klogg_candidate_lower)

    if(_klogg_candidate_lower MATCHES "(^|[^a-z0-9])(x86_64|amd64|x64)([^a-z0-9]|$)")
      set(${out_arch} "x64" PARENT_SCOPE)
      set(${out_source} "${_klogg_candidate}" PARENT_SCOPE)
      return()
    endif()

    if(_klogg_candidate_lower MATCHES "(^|[^a-z0-9])(aarch64|arm64)([^a-z0-9]|$)")
      set(${out_arch} "arm64" PARENT_SCOPE)
      set(${out_source} "${_klogg_candidate}" PARENT_SCOPE)
      return()
    endif()

    if(_klogg_candidate_lower MATCHES "(^|[^a-z0-9])(x86|i[3-6]86|win32)([^a-z0-9]|$)")
      set(${out_arch} "x86" PARENT_SCOPE)
      set(${out_source} "${_klogg_candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  if(KLOGG_ARCH EQUAL 64)
    set(${out_arch} "x64" PARENT_SCOPE)
    set(${out_source} "KLOGG_ARCH" PARENT_SCOPE)
  else()
    set(${out_arch} "x86" PARENT_SCOPE)
    set(${out_source} "KLOGG_ARCH" PARENT_SCOPE)
  endif()
endfunction()

if(WIN32)
  set(KLOGG_TARGET_OS "windows")
elseif(APPLE)
  set(KLOGG_TARGET_OS "macos")
elseif(UNIX)
  set(KLOGG_TARGET_OS "linux")
else()
  set(KLOGG_TARGET_OS "unknown")
endif()

klogg_normalize_target_arch(KLOGG_TARGET_ARCH KLOGG_TARGET_ARCH_SOURCE)

set(KLOGG_TARGET_IS_X64 OFF)
set(KLOGG_TARGET_IS_ARM64 OFF)
set(KLOGG_TARGET_IS_X86 OFF)
if(KLOGG_TARGET_ARCH STREQUAL "x64")
  set(KLOGG_TARGET_IS_X64 ON)
elseif(KLOGG_TARGET_ARCH STREQUAL "arm64")
  set(KLOGG_TARGET_IS_ARM64 ON)
elseif(KLOGG_TARGET_ARCH STREQUAL "x86")
  set(KLOGG_TARGET_IS_X86 ON)
endif()

set(_klogg_default_use_hyperscan OFF)
set(_klogg_default_use_vectorscan OFF)
set(KLOGG_DEFAULT_FAT_RUNTIME_MODE "off")

if(KLOGG_TARGET_OS STREQUAL "windows")
  if(KLOGG_TARGET_IS_X64)
    set(_klogg_default_use_hyperscan ON)
  endif()
elseif(KLOGG_TARGET_OS STREQUAL "linux")
  if(KLOGG_TARGET_IS_X64)
    set(_klogg_default_use_hyperscan ON)
    set(KLOGG_DEFAULT_FAT_RUNTIME_MODE "on_if_supported")
  elseif(KLOGG_TARGET_IS_ARM64)
    set(_klogg_default_use_vectorscan ON)
    set(KLOGG_DEFAULT_FAT_RUNTIME_MODE "on_if_supported")
  endif()
elseif(KLOGG_TARGET_OS STREQUAL "macos")
  if(KLOGG_TARGET_IS_X64)
    set(_klogg_default_use_hyperscan ON)
  elseif(KLOGG_TARGET_IS_ARM64)
    set(_klogg_default_use_vectorscan ON)
  endif()
endif()

option(KLOGG_USE_HYPERSCAN "Use Hyperscan" ${_klogg_default_use_hyperscan})
option(KLOGG_USE_VECTORSCAN "Use Vectorscan" ${_klogg_default_use_vectorscan})

if(KLOGG_USE_HYPERSCAN AND KLOGG_USE_VECTORSCAN)
  message(FATAL_ERROR "KLOGG_USE_HYPERSCAN and KLOGG_USE_VECTORSCAN are mutually exclusive")
endif()

if(KLOGG_TARGET_OS STREQUAL "windows" AND KLOGG_TARGET_IS_ARM64
   AND (KLOGG_USE_HYPERSCAN OR KLOGG_USE_VECTORSCAN))
  message(FATAL_ERROR "Windows arm64 is Qt-only in this task; disable KLOGG_USE_HYPERSCAN and KLOGG_USE_VECTORSCAN")
endif()

if(KLOGG_TARGET_IS_X86 AND (KLOGG_USE_HYPERSCAN OR KLOGG_USE_VECTORSCAN))
  message(FATAL_ERROR "Accelerated regex backends are only supported on x64 and arm64 targets")
endif()

if(KLOGG_USE_HYPERSCAN)
  set(KLOGG_REGEX_BACKEND "hyperscan")
  set(KLOGG_FAT_RUNTIME_MODE "${KLOGG_DEFAULT_FAT_RUNTIME_MODE}")
elseif(KLOGG_USE_VECTORSCAN)
  set(KLOGG_REGEX_BACKEND "vectorscan")
  set(KLOGG_FAT_RUNTIME_MODE "${KLOGG_DEFAULT_FAT_RUNTIME_MODE}")
else()
  set(KLOGG_REGEX_BACKEND "qt")
  set(KLOGG_FAT_RUNTIME_MODE "n/a")
endif()

message(
  STATUS
    "klogg backend selection: os=${KLOGG_TARGET_OS}, arch=${KLOGG_TARGET_ARCH} (${KLOGG_TARGET_ARCH_SOURCE}), backend=${KLOGG_REGEX_BACKEND}, fat-runtime=${KLOGG_FAT_RUNTIME_MODE}"
)

set(BUILD_VERSION
    $ENV{KLOGG_VERSION}
    CACHE STRING "build version"
)
set(BUILD_NUMBER
    ""
    CACHE STRING "build number"
)

set(NOTICE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE)
set(COPYING_FILE ${CMAKE_CURRENT_SOURCE_DIR}/COPYING)

set(README_FILE ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

set(DOCUMENTATION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/DOCUMENTATION.md)
set(DOCUMENTATION_HTML ${CMAKE_BINARY_DIR}/generated/documentation.html)
set(DOCUMENTATION_RESOURCE ${CMAKE_BINARY_DIR}/generated/documentation.qrc)

if(WIN32)
  set(ICON_NAME klogg.ico)
elseif(APPLE)
  set(ICON_NAME klogg.icns)
endif()

if(NOT
   ${ICON_NAME}
   STREQUAL
   ""
)
  set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Resources/${ICON_NAME})
endif()

if(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.14)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /experimental:external /external:W0 ")
  set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
endif()

set(out_dir "${CMAKE_BINARY_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${out_dir}")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

include(StandardProjectSettings)
include(ucm)
include(prepare_version)
include(FindPkgConfig)
include(GNUInstallDirs)

if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_CXX_FLAGS
              "${CMAKE_CXX_FLAGS}"
    )
  else(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")

  if(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_C_FLAGS
              "${CMAKE_C_FLAGS}"
    )
  else(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
  endif(CMAKE_C_FLAGS MATCHES "/W[0-4]")

  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /D __SSE4_1__=1 /D __SSE4_2__=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D __SSE4_1__=1 /D __SSE4_2__=1")
  endif()

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj ")
else()
  ucm_add_flags(CXX "-fno-sized-deallocation -fno-omit-frame-pointer")

  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _klogg_system_processor)
  if(_klogg_system_processor MATCHES "^(arm64|aarch64)$")
    if(KLOGG_GENERIC_CPU)
      ucm_add_flags(C CXX "-march=armv8-a -mtune=generic")
    else()
      ucm_add_flags(C CXX "-march=native -mtune=generic")
    endif()
  elseif(_klogg_system_processor MATCHES "^(x86_64|amd64|x86|i[3-6]86)$")
    ucm_add_flags(C CXX "-mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt")
    if(KLOGG_GENERIC_CPU)
      ucm_add_flags(C CXX "-march=x86-64 -mtune=generic")
    else()
      ucm_add_flags(C CXX "-march=native -mtune=generic")
    endif()
  else()
    ucm_add_flags(C CXX "-march=native -mtune=generic")
  endif()
  unset(_klogg_system_processor)
endif(MSVC)

if(WIN32)
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/win)
elseif(APPLE)
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/mac)
else()
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/linux)
endif()

message("Dump symbols tools path ${SYMS_TOOLS}")

# standard compiler warnings
# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
include(CompilerWarnings)
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

# Link this 'library' to set the c++ standard / compile-time options requested
include(CompilerFlags)
add_library(project_options INTERFACE)
set_project_compile_flags(project_options)

# sanitizer options if supported by compiler
include(Sanitizers)
enable_sanitizers(project_options)

# allow for static analysis options
include(StaticAnalyzers)

ucm_print_flags()

if(APPLE)
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    message("Adding missing Threads workaround")
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
  endif()
endif()

find_package(
  QT
  NAMES
  Qt6
  Qt5
  COMPONENTS Core
             Widgets
             Concurrent
             Network
             SerialPort
             Xml
  REQUIRED
)
find_package(
  Qt${QT_VERSION_MAJOR}
  COMPONENTS Core
             Widgets
             Concurrent
             Network
             SerialPort
             Xml
  REQUIRED
)

set(KLOGG_QT_VERSION ${Qt${QT_VERSION_MAJOR}Core_VERSION})

message("Qt version ${KLOGG_QT_VERSION}")
message("LTO ${KLOGG_USE_LTO}")
if(WIN32)
  # Support Debug app builds with single-config (release-only) Qt installations.
  # In that setup Qt debug libraries are absent and Debug iterator checks can reference
  # private symbols not exported by release Qt DLLs.
  set(_klogg_qt_core_debug_implib "")
  get_target_property(_klogg_qt_core_debug_implib Qt${QT_VERSION_MAJOR}::Core IMPORTED_IMPLIB_DEBUG)
  if(NOT _klogg_qt_core_debug_implib)
    message(WARNING "Qt debug libraries are not available; defining QT_NO_DEBUG for Debug builds.")
    add_compile_definitions($<$<CONFIG:Debug>:QT_NO_DEBUG>)
  endif()
  unset(_klogg_qt_core_debug_implib)
endif()

if(APPLE)
  if(${KLOGG_OSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET ${KLOGG_OSX_DEPLOYMENT_TARGET})
    message("OSX target ${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR OFF)
set(BUILD_SHARED_LIBS OFF)

add_custom_target(ci_build)

add_subdirectory(3rdparty)
add_subdirectory(src)
add_subdirectory(website)

enable_testing()
if(NOT (WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]64$"))
  add_test(NAME klogg_smoke COMMAND klogg -platform offscreen -v
          WORKING_DIRECTORY "$<TARGET_FILE_DIR:klogg>"
  )
endif()
# set_tests_properties(klogg_smoke PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:klogg>")

if(${KLOGG_BUILD_TESTS})
  add_subdirectory(tests)
endif()

# Installation and packaging
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)
set(CPACK_SOURCE_IGNORE_FILES "build_root;.git")

set(CPACK_OUTPUT_FILE_PREFIX "packages")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR ${COMPANY})
set(CPACK_PACKAGE_CONTACT "dm17ryk@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/description.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${COPYING_FILE}")
set(CPACK_PACKAGE_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}"
)
set(CPACK_PACKAGE_ICON "${ICON_FILE}")

if(UNIX)
  include(InstallRequiredSystemLibraries)

  set(CPACK_STRIP_FILES "bin/klogg")

  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_VOLUME_NAME "${PROJECT_NAME}")
    set(CPACK_SYSTEM_NAME "OSX")
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/osx/dmg_background.tif")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/packaging/osx/dmg_setup.scpt")

  else()
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/16x16/klogg.png
            DESTINATION share/icons/hicolor/16x16/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/32x32/klogg.png
            DESTINATION share/icons/hicolor/32x32/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/48x48/klogg.png
            DESTINATION share/icons/hicolor/48x48/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/scalable/klogg.svg
            DESTINATION share/icons/hicolor/scalable/apps
    )

    install(
      FILES ${COPYING_FILE}
            ${NOTICE_FILE}
            ${README_FILE}
            ${DOCUMENTATION_HTML}
            ${DOCUMENTATION_FILE}
      DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/klogg.desktop DESTINATION share/applications)

    set(CPACK_GENERATOR DEB)

    #deb/rpm architecture mapping
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _klogg_system_processor)
    set(_klogg_deb_arch "${_klogg_system_processor}")
    set(_klogg_rpm_arch "${_klogg_system_processor}")

    if(_klogg_system_processor MATCHES "^(x86_64|amd64)$")
      set(_klogg_deb_arch "amd64")
      set(_klogg_rpm_arch "x86_64")
    elseif(_klogg_system_processor MATCHES "^(aarch64|arm64)$")
      set(_klogg_deb_arch "arm64")
      set(_klogg_rpm_arch "aarch64")
    endif()

    #deb
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_klogg_deb_arch}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "http://github.com/dm17ryk/klogg")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dmitry Kokotov <${CPACK_PACKAGE_CONTACT}>")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS YES)
    set(_klogg_qt_private_dirs "")
    foreach(_qt_component Core Widgets Gui Network SerialPort Xml Core5Compat)
      if(TARGET Qt${QT_VERSION_MAJOR}::${_qt_component})
        set(_qt_lib_location "")
        foreach(_qt_location_prop
                IMPORTED_LOCATION
                IMPORTED_LOCATION_RELEASE
                IMPORTED_LOCATION_RELWITHDEBINFO
                IMPORTED_LOCATION_MINSIZEREL
                IMPORTED_LOCATION_DEBUG)
          get_target_property(
            _qt_lib_location
            Qt${QT_VERSION_MAJOR}::${_qt_component}
            ${_qt_location_prop}
          )
          if(_qt_lib_location
             AND NOT _qt_lib_location MATCHES "-NOTFOUND$"
             AND EXISTS "${_qt_lib_location}")
            break()
          endif()
          set(_qt_lib_location "")
        endforeach()

        if(_qt_lib_location)
          get_filename_component(_qt_lib_dir "${_qt_lib_location}" DIRECTORY)
          list(APPEND _klogg_qt_private_dirs "${_qt_lib_dir}")
        endif()
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _klogg_qt_private_dirs)
    if(_klogg_qt_private_dirs)
      set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${_klogg_qt_private_dirs}")
      message(
        STATUS
          "CPack DEB shlibdeps private dirs: ${CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS}"
      )
    endif()

    #RPM
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "${_klogg_rpm_arch}")
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
    set(CPACK_RPM_PACKAGE_AUTOREQ YES)
    #set(CPACK_RPM_PACKAGE_REQUIRES "qt5-qtbase >= ${KLOGG_QT_VERSION}")
    set(CPACK_RPM_PACKAGE_URL "http://github.com/dm17ryk/klogg")
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/16x16
        /usr/share/icons/hicolor/16x16/apps
        /usr/share/icons/hicolor/32x32
        /usr/share/icons/hicolor/32x32/apps
        /usr/share/icons/hicolor/scalable
        /usr/share/icons/hicolor/scalable/apps
    )
  endif()
  include(CPack)
endif()

# for IDE grouping
add_custom_target(
  installation
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/klogg.nsi
          ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/klogg.desktop
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in
)

add_custom_target(
  klogg_docs
  SOURCES ${README_FILE}
          ${COPYING_FILE}
          ${NOTICE_FILE}
          ${DOCUMENTATION_FILE}
)

add_custom_target(klogg_stuff SOURCES .clang-format .clang-tidy .cmake-format)

