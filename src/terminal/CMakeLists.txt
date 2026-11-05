add_library(cilogg_terminal_model STATIC terminalemulator.cpp terminalemulator.h)
target_include_directories(cilogg_terminal_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cilogg_terminal_model PUBLIC Qt${QT_VERSION_MAJOR}::Core
  PRIVATE cilogg_vterm project_options project_warnings)
