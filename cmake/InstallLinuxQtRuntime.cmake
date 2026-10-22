# Included after all application targets and their install rules exist.
qt6_generate_deploy_script(
  TARGET
  klogg
  OUTPUT_SCRIPT
  _klogg_deploy_script
  CONTENT
  "
set(QT_DEPLOY_BIN_DIR \"${CMAKE_INSTALL_BINDIR}\")
set(QT_DEPLOY_LIB_DIR \"${KLOGG_PRIVATE_DIR}/lib\")
set(QT_DEPLOY_PLUGINS_DIR \"${KLOGG_PRIVATE_DIR}/qt-plugins\")
message(STATUS \"Deploying private Qt runtime: prefix=\${QT_DEPLOY_PREFIX}, bin=\${QT_DEPLOY_BIN_DIR}, lib=\${QT_DEPLOY_LIB_DIR}\")
qt6_deploy_runtime_dependencies(
  EXECUTABLE \"$<TARGET_FILE:klogg>\"
  ADDITIONAL_EXECUTABLES \"$<TARGET_FILE:klogg_grep>\"
                        \"$<TARGET_FILE:cilogg_updater>\"
  NO_TRANSLATIONS
  EXCLUDE_PLUGIN_TYPES sqldrivers
  INCLUDE_PLUGINS qsqlite qoffscreen
  VERBOSE
)
"
)
install(SCRIPT "${_klogg_deploy_script}")
configure_file(cmake/InstallPrivateQtConfig.cmake.in "${CMAKE_BINARY_DIR}/InstallPrivateQtConfig.cmake" @ONLY)
install(SCRIPT "${CMAKE_BINARY_DIR}/InstallPrivateQtConfig.cmake")

file(
  RELATIVE_PATH
  _klogg_public_to_private
  "/${KLOGG_PUBLIC_BINDIR}"
  "/${CMAKE_INSTALL_BINDIR}"
)
foreach(_klogg_command "${PRODUCT_EXECUTABLE_NAME}" "${PRODUCT_GREP_EXECUTABLE_NAME}")
  configure_file(cmake/LinuxCommandLauncher.sh.in "${CMAKE_BINARY_DIR}/${_klogg_command}-launcher" @ONLY)
  install(
    PROGRAMS "${CMAKE_BINARY_DIR}/${_klogg_command}-launcher"
    DESTINATION "${KLOGG_PUBLIC_BINDIR}"
    RENAME "${_klogg_command}"
  )
endforeach()
