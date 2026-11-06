find_package(Threads REQUIRED)
add_executable(cilogg_terminal_tests tests_main.cpp vterm_bounds_test.cpp terminalemulator_test.cpp)
target_link_libraries(cilogg_terminal_tests PRIVATE cilogg_terminal_model cilogg_vterm
  Catch2 Threads::Threads project_options project_warnings)
# Keep the helper and its Qt runtime outside the packaged application directory.
set_target_properties(cilogg_terminal_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/terminal_tests/$<CONFIG>")
if(WIN32)
  target_compile_definitions(cilogg_terminal_tests PRIVATE NOMINMAX)
  get_target_property(terminal_qmake Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
  get_filename_component(terminal_qt_bin "${terminal_qmake}" DIRECTORY)
  add_custom_command(TARGET cilogg_terminal_tests POST_BUILD
    COMMAND "${terminal_qt_bin}/windeployqt.exe" --no-translations --no-plugins
      "$<TARGET_FILE:cilogg_terminal_tests>"
    VERBATIM)
endif()
add_dependencies(ci_build cilogg_terminal_tests)
add_test(NAME terminal_model_tests COMMAND $<TARGET_FILE:cilogg_terminal_tests>)
set_tests_properties(terminal_model_tests PROPERTIES TIMEOUT 60 LABELS "terminal;UT01")
if(Python3_Interpreter_FOUND)
  add_test(NAME terminal_vendor_lock_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_vendor_lock.py")
  set_tests_properties(terminal_vendor_lock_tests PROPERTIES TIMEOUT 30 LABELS "terminal;python")
endif()
