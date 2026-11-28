# updatefiles.cpp and installprepare.cpp are Qt-free and shared with klogg_versioncheck, so CILogg
# and the helper apply the same claim, lock, package verification and file replacement rules.
add_executable(
  cilogg_updater main.cpp "${CMAKE_SOURCE_DIR}/src/versioncheck/src/updatefiles.cpp"
                 "${CMAKE_SOURCE_DIR}/src/versioncheck/src/installprepare.cpp"
)
target_compile_features(cilogg_updater PRIVATE cxx_std_17)
target_include_directories(cilogg_updater PRIVATE "${CMAKE_SOURCE_DIR}/src/versioncheck/include")
set_target_properties(cilogg_updater PROPERTIES OUTPUT_NAME "cilogg_updater")

if(WIN32)
  target_link_libraries(cilogg_updater PRIVATE shell32 ole32 rstrtmgr)
endif()
if(MSVC)
  # The installer runs the helper from its temporary folder and the update runs it from the
  # update folder: neither has the Visual C++ runtime DLLs next to it.
  set_property(TARGET cilogg_updater PROPERTY MSVC_RUNTIME_LIBRARY
                                              "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

install(TARGETS cilogg_updater RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
