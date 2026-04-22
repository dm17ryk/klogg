if(APPLE)
  set(OS_BUNDLE MACOSX_BUNDLE)
elseif(WIN32)
  set(OS_BUNDLE WIN32)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/documentation.qrc.in ${DOCUMENTATION_RESOURCE} @ONLY)

if(KLOGG_BUILD_DOCUMENTATION)
  add_custom_command(
    OUTPUT ${DOCUMENTATION_HTML}
    COMMAND $<TARGET_FILE:maddy> ${DOCUMENTATION_FILE} ${DOCUMENTATION_HTML}
    DEPENDS maddy ${DOCUMENTATION_FILE}
  )
else()
  file(
    WRITE "${DOCUMENTATION_HTML}"
    "<html><body><p>Documentation generation disabled for this build.</p></body></html>\n"
  )
endif()

set(TS_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/en.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh_CN.ts
)

option(KLOGG_BUILD_TRANSLATIONS "Build i18n translation resources" ON)
if(KLOGG_BUILD_TRANSLATIONS)
  find_package(Qt${QT_VERSION_MAJOR}LinguistTools REQUIRED)
  if (${QT_VERSION_MAJOR} EQUAL 5)
    qt5_add_translation(QM_FILES ${TS_FILES})
  else (${QT_VERSION_MAJOR} EQUAL 6)
    qt6_add_translation(QM_FILES ${TS_FILES})
  endif()

  if (NOT TARGET Qt${QT_VERSION_MAJOR}::lupdate)
      message(FATAL_ERROR "The package \"Qt${QT_VERSION_MAJOR}LinguistTools\" is required.")
  endif()
  set(Qt_LUPDATE_EXECUTABLE Qt${QT_VERSION_MAJOR}::lupdate)
  set(Qt_LCONVERT_EXECUTABLE Qt${QT_VERSION_MAJOR}::lconvert)
  add_custom_target(lupdate
      COMMAND ${Qt_LUPDATE_EXECUTABLE} -recursive -no-obsolete ${CMAKE_SOURCE_DIR}/src -ts ${TS_FILES}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
      COMMENT "Updating translations"
  )

  include(TranslationUtils)
  add_app_translations_resource(KLOGG_I18N_RES ${QM_FILES})
  add_qt_translations_resource(KLOGG_QT_TRANSLATION_RES zh_CN)
endif()

set(MAIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cli.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/klogg.qrc 
                 ${KLOGG_I18N_RES}
                 ${KLOGG_QT_TRANSLATION_RES})

set(KLOGG_UI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/messagereceiver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/kloggapp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/labagentrunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/labagentrunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/labbundleutils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/labbundleutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/labcontrollerservice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/labcontrollerservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/labrequest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/scenariobatchrequest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/scenarioheadlessrunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/scenarioheadlessrunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${DOCUMENTATION_RESOURCE}
    ${ICON_FILE}
)

set(KLOGG_GREP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/klogg_grep.cpp)
set(KLOGG_PYTHON_RUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/python_runtime)

set(MAIN_LIBS
    klogg_logdata
    klogg_crash_handler
    roaring
    kdsingleapp
    Qt${QT_VERSION_MAJOR}::Sql
)

add_library(
  klogg_commander STATIC
  ${CMAKE_SOURCE_DIR}/src/ui/include/commander.h
  ${CMAKE_SOURCE_DIR}/src/ui/src/commander.cpp
)

add_library(
  klogg_lab STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/labclient.h
  ${CMAKE_CURRENT_SOURCE_DIR}/labclient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/labtypes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/labtypes.cpp
)

target_include_directories(klogg_commander PUBLIC ${CMAKE_SOURCE_DIR}/src/ui/include)
target_include_directories(
  klogg_lab
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_SOURCE_DIR}/src/ui/include
)
target_link_libraries(
  klogg_commander
  PUBLIC project_options
         project_warnings
         Qt${QT_VERSION_MAJOR}::Core
         Qt${QT_VERSION_MAJOR}::Sql
         Qt${QT_VERSION_MAJOR}::SerialPort
)
target_link_libraries(
  klogg_lab
  PUBLIC project_options
         project_warnings
         Qt${QT_VERSION_MAJOR}::Core
         Qt${QT_VERSION_MAJOR}::Network
         Qt${QT_VERSION_MAJOR}::SerialPort
)

add_executable(klogg ${OS_BUNDLE} ${MAIN_SOURCES} ${KLOGG_UI_SOURCES})
add_executable(klogg_portable ${OS_BUNDLE} ${MAIN_SOURCES} ${KLOGG_UI_SOURCES})
add_executable(klogg_grep ${MAIN_SOURCES} ${KLOGG_GREP_SOURCES})

add_dependencies(ci_build klogg klogg_grep)

if(WIN32)
  add_dependencies(ci_build klogg_portable)
endif()

set_target_properties(klogg PROPERTIES AUTORCC ON)
set_target_properties(klogg PROPERTIES AUTOMOC ON)
set_target_properties(klogg_portable PROPERTIES AUTORCC ON)
set_target_properties(klogg_portable PROPERTIES AUTOMOC ON)
set_target_properties(klogg_grep PROPERTIES AUTORCC ON)
set_target_properties(klogg_grep PROPERTIES AUTOMOC ON)
set_target_properties(
  klogg
  PROPERTIES OUTPUT_NAME "${PRODUCT_EXECUTABLE_NAME}"
)
set_target_properties(
  klogg_portable
  PROPERTIES OUTPUT_NAME "${PRODUCT_PORTABLE_EXECUTABLE_NAME}"
)
set_target_properties(
  klogg_grep
  PROPERTIES OUTPUT_NAME "${PRODUCT_GREP_EXECUTABLE_NAME}"
)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET klogg_portable PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET klogg_grep PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_link_libraries(klogg PUBLIC ${MAIN_LIBS} klogg_ui klogg_lab)
target_link_libraries(klogg_portable PUBLIC ${MAIN_LIBS} klogg_ui klogg_lab)
target_link_libraries(klogg_grep PUBLIC ${MAIN_LIBS} klogg_commander)

target_compile_definitions(klogg_portable PUBLIC -DKLOGG_PORTABLE)

if(WIN32)

  target_sources(klogg PRIVATE ${ProductVersionResourceFiles})
  target_sources(klogg_portable PRIVATE ${ProductVersionResourceFiles})
  target_sources(klogg_grep PRIVATE ${ProductVersionResourceFiles})

elseif(APPLE)
  set_source_files_properties(${ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties(${NOTICE_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${COPYING_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set_source_files_properties(${README_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_HTML} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set(MACOSX_BUNDLE_BUNDLE_DISPLAY_NAME ${PRODUCT_DISPLAY_NAME})
  set(MACOSX_BUNDLE_BUNDLE_NAME ${PRODUCT_DISPLAY_NAME})
  set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
  set(MACOSX_BUNDLE_COPYRIGHT ${COPYRIGHT})
  set(MACOSX_BUNDLE_GUI_IDENTIFIER ${IDENTIFIER})
  set(MACOSX_BUNDLE_ICON_FILE ${ICON_NAME})

endif()

install(
  TARGETS klogg
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)

if(WIN32)
  if(TARGET tbb)
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbb> $<TARGET_FILE_DIR:klogg>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbb"
    )
  endif()
  if(TARGET tbbmalloc_proxy)
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbbmalloc> $<TARGET_FILE_DIR:klogg>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbbmalloc"
    )
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbbmalloc_proxy> $<TARGET_FILE_DIR:klogg>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbbmalloc_proxy"
    )
  endif()

  add_custom_command(
    TARGET klogg
    POST_BUILD
    COMMAND cmake -E copy_directory ${KLOGG_PYTHON_RUNTIME_DIR} $<TARGET_FILE_DIR:klogg>/python_runtime
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Copy Python runtime files"
  )

  add_custom_command(
    TARGET klogg_portable
    POST_BUILD
    COMMAND cmake -E copy_directory ${KLOGG_PYTHON_RUNTIME_DIR} $<TARGET_FILE_DIR:klogg_portable>/python_runtime
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Copy Python runtime files"
  )
endif()

if(KLOGG_USE_SENTRY)
  if(NOT WIN32)
    if(APPLE)
      find_program(DSYMUTIL dsymutil)
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${DSYMUTIL} --out=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${PRODUCT_EXECUTABLE_NAME}.dSym $<TARGET_FILE:klogg>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg>"
      )

      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${DSYMUTIL} --out=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${PRODUCT_PORTABLE_EXECUTABLE_NAME}.dSym $<TARGET_FILE:klogg_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg_portable>"
      )
    else()
      find_program(OBJCOPY objcopy)
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${OBJCOPY} --only-keep-debug --compress-debug-sections=zlib $<TARGET_FILE:klogg>
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${PRODUCT_EXECUTABLE_NAME}.debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg>"
      )
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${OBJCOPY} --strip-debug --strip-unneeded $<TARGET_FILE:klogg>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Stripping $<TARGET_FILE:klogg>"
      )
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND ${OBJCOPY} --add-gnu-debuglink=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${PRODUCT_EXECUTABLE_NAME}.debug $<TARGET_FILE:klogg>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Linking symbols for $<TARGET_FILE:klogg>"
      )

      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --only-keep-debug $<TARGET_FILE:klogg_portable>
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${PRODUCT_PORTABLE_EXECUTABLE_NAME}.debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:klogg>"
      )
      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --strip-debug --strip-unneeded $<TARGET_FILE:klogg_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Stripping $<TARGET_FILE:klogg_portable>"
      )
      add_custom_command(
        TARGET klogg_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --add-gnu-debuglink=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${PRODUCT_PORTABLE_EXECUTABLE_NAME}.debug
                $<TARGET_FILE:klogg_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Linking symbols for $<TARGET_FILE:klogg_portable>"
      )
    endif()

    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:klogg>/${PRODUCT_CRASHPAD_HANDLER_NAME}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy crash_handler"
    )

    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump $<TARGET_FILE_DIR:klogg>/${PRODUCT_MINIDUMP_DUMP_NAME}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy stackwalk"
    )
  else()
    add_custom_command(
      TARGET klogg
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:klogg>/${PRODUCT_CRASHPAD_HANDLER_NAME}.exe
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy crash_handler"
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump_x64.exe $<TARGET_FILE_DIR:klogg>/${PRODUCT_MINIDUMP_DUMP_NAME}.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Copy stackwalk"
      )
    else()
      add_custom_command(
        TARGET klogg
        POST_BUILD
        COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump_x86.exe $<TARGET_FILE_DIR:klogg>/${PRODUCT_MINIDUMP_DUMP_NAME}.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Copy stackwalk"
      )
    endif()
  endif()

  if(APPLE)
    install(PROGRAMS $<TARGET_FILE_DIR:klogg>/${PRODUCT_MINIDUMP_DUMP_NAME} $<TARGET_FILE_DIR:klogg>/${PRODUCT_CRASHPAD_HANDLER_NAME}
            DESTINATION $<TARGET_BUNDLE_CONTENT_DIR:klogg>/MacOS
    )
  elseif(NOT WIN32)
    install(PROGRAMS $<TARGET_FILE_DIR:klogg>/${PRODUCT_MINIDUMP_DUMP_NAME} $<TARGET_FILE_DIR:klogg>/${PRODUCT_CRASHPAD_HANDLER_NAME}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endif(KLOGG_USE_SENTRY)
