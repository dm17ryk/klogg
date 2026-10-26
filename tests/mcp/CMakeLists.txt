find_package(Python3 3.9 QUIET COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
  add_test(
    NAME workbench_parity_self_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}
            -p test_workbench_*.py -v
  )
  set_tests_properties(workbench_parity_self_tests PROPERTIES TIMEOUT 300 LABELS "workbench;G03;harness")
endif()

option(KLOGG_TEST_WORKBENCH_PARITY "Run isolated native GUI/CLI/MCP parity (requires deployed Qt runtime)" OFF)
if(KLOGG_TEST_WORKBENCH_PARITY)
  if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR "Workbench parity requires Python 3.9 or newer")
  endif()
  if(KLOGG_USE_HYPERSCAN OR KLOGG_USE_VECTORSCAN)
    set(workbench_backend accelerated)
  else()
    set(workbench_backend qt)
  endif()
  add_test(
    NAME workbench_parity_native
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/workbench_parity.py
            --executable $<TARGET_FILE:klogg> --task G03
            --backend ${workbench_backend} --artifacts ${CMAKE_BINARY_DIR}/workbench-parity
            --new-run
  )
  set_tests_properties(workbench_parity_native PROPERTIES TIMEOUT 330 LABELS "workbench;G03;native;${workbench_backend}")
endif()
