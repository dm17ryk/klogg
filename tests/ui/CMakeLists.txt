set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
)

if(NOT APPLE)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow_test.cpp)
endif()

add_executable(cilogg_itests ${UI_TEST_SOURCES} ${BACKWARD_ENABLE})

target_link_libraries(cilogg_itests klogg_ui klogg_utils Catch2 Qt${QT_VERSION_MAJOR}::Test test_utils)
set_target_properties(
    cilogg_itests
    PROPERTIES AUTOMOC ON
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
        COMMAND $<TARGET_FILE:cilogg_itests> "[ui]" -platform offscreen
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
