set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdatasource_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analysisjob_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analysisregex_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/analysiscommands_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/valuecounts_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/valuecountdock_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/regexlabdock_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filewatcher_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logviewperformance_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/workbenchbenchmark_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/workbenchusability_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/configurationproject_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/uiautomation_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins_ui_test.cpp
)

if(NOT APPLE)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/regexlabwindow_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/valuecountswindow_test.cpp)
endif()

add_executable(cilogg_itests ${UI_TEST_SOURCES} ${BACKWARD_ENABLE}
    ${PROJECT_SOURCE_DIR}/src/app/klogg.qrc)
add_executable(analysis_worker_fixture ../analysis_worker_fixture.cpp)
target_link_libraries(analysis_worker_fixture PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(cilogg_itests PRIVATE
    ANALYSIS_WORKER_PATH="$<TARGET_FILE:klogg>"
    ANALYSIS_FIXTURE_PATH="$<TARGET_FILE:analysis_worker_fixture>")
add_dependencies(cilogg_itests klogg analysis_worker_fixture)

target_link_libraries(cilogg_itests klogg_ui klogg_utils Catch2 Qt${QT_VERSION_MAJOR}::Test test_utils)
set_target_properties(
    cilogg_itests
    PROPERTIES AUTOMOC ON
               AUTORCC ON
               OUTPUT_NAME "${PRODUCT_UI_TEST_EXECUTABLE_NAME}"
)

add_backward(cilogg_itests)

if(WIN32)
    # Keep the log-data and widget suites in separate processes.  Both suites
    # use global Qt/Hyperscan state (QSettings, highlighter databases and the
    # global thread pool); running them together can leave the later MainWindow
    # scenarios compiling a database indefinitely on Windows CI.  The binary
    # remains the same, but each CTest entry gets a fresh process and event
    # loop.
    add_test(
        NAME cilogg_itests
        COMMAND $<TARGET_FILE:cilogg_itests> "[logdata]" -platform offscreen
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:klogg>"
    )
    add_test(
        NAME cilogg_ui_itests
        COMMAND $<TARGET_FILE:cilogg_itests> "[ui]~[workbench-native]" -platform offscreen
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:klogg>"
    )
    set_tests_properties(cilogg_itests cilogg_ui_itests PROPERTIES TIMEOUT 900)
else()
    add_test(
        NAME cilogg_itests
        COMMAND $<TARGET_FILE:cilogg_itests> -platform offscreen
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:klogg>"
    )
    set_tests_properties(cilogg_itests PROPERTIES TIMEOUT 900)
endif()
# set_tests_properties(cilogg_itests PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:cilogg_itests>")
