# Add test cpp file
add_executable(preview_decoder_fixture previewdecoder_fixture.cpp)
target_link_libraries(preview_decoder_fixture PRIVATE Qt${QT_VERSION_MAJOR}::Core)

add_executable(cilogg_tests
    actionsconfig_test.cpp
    actionparametersdialog_test.cpp
    actionparameterseditorwidget_test.cpp
    commander_test.cpp
    fieldvalue_test.cpp
    datasetcontracts_test.cpp
    valuecounts_commands_test.cpp
    plugins_test.cpp
    mcp_test.cpp
    configuration_test.cpp
    captureevaluator_test.cpp
    configurationexport_test.cpp
    encodingdetector_test.cpp
    importpreviewsdialog_test.cpp
    linepositionarray_test.cpp
    patternmatcher_test.cpp
    previewdecodeutils_test.cpp
    previewmatchstage_test.cpp
    previewassembly_test.cpp
    previewservice_test.cpp
    previewdecoder_test.cpp
    previewsource_test.cpp
    previewcommands_test.cpp
    serialcapturesettings_test.cpp
    streamsession_actions_test.cpp
    sessioninfo_test.cpp
    versionchecker_test.cpp
    wrappedstring_test.cpp
    datasetreference_test.cpp
    tests_main.cpp
)

set_exact_floating_point("${CMAKE_CURRENT_SOURCE_DIR}/fieldvalue_test.cpp")
set_exact_floating_point("${CMAKE_CURRENT_SOURCE_DIR}/datasetcontracts_test.cpp")

target_link_libraries(cilogg_tests klogg_ui klogg_utils klogg_logging klogg_mcp Catch2 Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(cilogg_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_compile_definitions(cilogg_tests PRIVATE PLUGIN_FIXTURE_PATH="$<TARGET_FILE:plugin_fixture>")
target_compile_definitions(cilogg_tests PRIVATE
    PREVIEW_APP_PATH="$<TARGET_FILE:klogg>"
    PREVIEW_PUBLIC_LUA_PATH="${CMAKE_SOURCE_DIR}/test_data/message_decoders/pdu_probe.lua"
    PREVIEW_DECODER_FIXTURE_PATH="$<TARGET_FILE:preview_decoder_fixture>"
    PREVIEW_FIXTURE_PATH="$<TARGET_FILE:preview_worker_fixture>")
add_dependencies(cilogg_tests plugin_fixture preview_worker_fixture preview_decoder_fixture klogg)
set_target_properties(
    cilogg_tests
    PROPERTIES AUTOMOC ON
               OUTPUT_NAME "${PRODUCT_UNIT_TEST_EXECUTABLE_NAME}"
)

add_test(
    NAME cilogg_tests
    COMMAND $<TARGET_FILE:cilogg_tests> -platform offscreen
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:klogg>"
)
set_tests_properties(cilogg_tests PROPERTIES TIMEOUT 180)
# set_tests_properties(cilogg_tests PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:cilogg_tests>")

set_exact_floating_point("${CMAKE_CURRENT_SOURCE_DIR}/datasetreference_test.cpp")
