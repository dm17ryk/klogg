# Native packages built with a vendor SDK must not depend on the distribution's
# older Qt ABI. Keep the complete application directory and Qt configuration private.
option(KLOGG_BUNDLE_QT_RUNTIME "Bundle the Qt runtime in a private Linux package directory" OFF)
if(NOT KLOGG_BUNDLE_QT_RUNTIME)
  return()
endif()

if(NOT
   CMAKE_SYSTEM_NAME
   STREQUAL
   "Linux"
   OR NOT
      QT_VERSION_MAJOR
      EQUAL
      6
   OR KLOGG_QT_VERSION VERSION_LESS "6.10"
   OR CMAKE_VERSION VERSION_LESS "3.22"
)
  message(FATAL_ERROR "Private Qt deployment requires Linux, Qt 6.10+ and CMake 3.22+")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
  message(FATAL_ERROR "Private Qt deployment requires prefix-relative bin and lib directories")
endif()

set(KLOGG_PUBLIC_BINDIR "${CMAKE_INSTALL_BINDIR}")
set(KLOGG_PRIVATE_DIR "${CMAKE_INSTALL_LIBDIR}/${PRODUCT_EXECUTABLE_NAME}")
set(CMAKE_INSTALL_BINDIR "${KLOGG_PRIVATE_DIR}/bin")
# This applies to every executable, including grep and updater helpers. Installed
# plugin examples and SDK files already follow CMAKE_INSTALL_BINDIR.
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
message(
  STATUS
    "Private Qt deployment: Qt=${KLOGG_QT_VERSION}, bin=${CMAKE_INSTALL_BINDIR}, lib=${KLOGG_PRIVATE_DIR}/lib, public=${KLOGG_PUBLIC_BINDIR}"
)
