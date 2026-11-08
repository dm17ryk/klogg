add_executable(cilogg_communication_tests tests_main.cpp communicationtypes_test.cpp sharedwriter_test.cpp
  serialwritedriver_test.cpp endpointlease_test.cpp statuscompatibility_test.cpp)
target_link_libraries(cilogg_communication_tests PRIVATE cilogg_communication_contracts
  cilogg_shared_writer cilogg_serial_write_driver cilogg_stream_registry Catch2 project_options project_warnings)
set_target_properties(cilogg_communication_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/communication_tests/$<CONFIG>")
if(WIN32)
  target_compile_definitions(cilogg_communication_tests PRIVATE NOMINMAX)
  get_target_property(communication_qmake Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
  get_filename_component(communication_qt_bin "${communication_qmake}" DIRECTORY)
  add_custom_command(TARGET cilogg_communication_tests POST_BUILD
    COMMAND "${communication_qt_bin}/windeployqt.exe" --no-translations --no-plugins
      "$<TARGET_FILE:cilogg_communication_tests>"
    VERBATIM)
endif()
add_dependencies(ci_build cilogg_communication_tests)
add_test(NAME communication_contract_tests COMMAND $<TARGET_FILE:cilogg_communication_tests>)
set_tests_properties(communication_contract_tests PROPERTIES TIMEOUT 60 LABELS "communication;D01a;D01b")
