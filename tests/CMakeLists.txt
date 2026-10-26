find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

add_subdirectory(helpers)
add_subdirectory(unit)
add_subdirectory(ui)
add_subdirectory(mcp)

add_dependencies(cilogg_itests file_write_helper)
add_dependencies(ci_build cilogg_tests cilogg_itests)

find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME workbench_benchmark_tests
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks" -v)
    # The 37-test suite measured 480s locally; RSS adds supervised process cases.
    # This aggregate cap does not change any individual probe or product deadline.
    set_tests_properties(workbench_benchmark_tests PROPERTIES TIMEOUT 900 LABELS "benchmarks;python")
else()
    message(STATUS "Workbench benchmark self-tests skipped: Python 3.9+ is unavailable")
endif()






