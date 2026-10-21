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
  GENERATE_QT_CONF
  NO_TRANSLATIONS
  EXCLUDE_PLUGIN_TYPES sqldrivers
  INCLUDE_PLUGINS qsqlite
  VERBOSE
)
"
)
install(SCRIPT "${_klogg_deploy_script}")

file(
  RELATIVE_PATH
  _klogg_public_to_private
  "/${KLOGG_PUBLIC_BINDIR}"
  "/${CMAKE_INSTALL_BINDIR}"
)
foreach(_klogg_command "${PRODUCT_EXECUTABLE_NAME}" "${PRODUCT_GREP_EXECUTABLE_NAME}")
  install(
    CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${KLOGG_PUBLIC_BINDIR}\")
    file(CREATE_LINK \"${_klogg_public_to_private}/${_klogg_command}\"
      \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${KLOGG_PUBLIC_BINDIR}/${_klogg_command}\"
      SYMBOLIC RESULT _klogg_link_result)
    if(NOT _klogg_link_result STREQUAL \"0\")
      message(FATAL_ERROR \"Cannot install ${_klogg_command} command link: \${_klogg_link_result}\")
    endif()
    message(STATUS \"Installed command link: ${KLOGG_PUBLIC_BINDIR}/${_klogg_command} -> ${_klogg_public_to_private}/${_klogg_command}\")
  "
  )
endforeach()
