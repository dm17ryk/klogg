add_executable(cilogg_updater main.cpp)
target_compile_features(cilogg_updater PRIVATE cxx_std_17)
target_include_directories(cilogg_updater PRIVATE "${CMAKE_SOURCE_DIR}/src/versioncheck/include")
set_target_properties(cilogg_updater PROPERTIES OUTPUT_NAME "cilogg_updater")

if(WIN32)
  target_link_libraries(cilogg_updater PRIVATE shell32)
endif()

install(TARGETS cilogg_updater RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
