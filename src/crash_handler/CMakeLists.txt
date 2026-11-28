add_library(
  klogg_crash_handler STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/crashdumps.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/crashhandler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/diagnostics.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/issuereporter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/memory_info.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/crashdumps.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/issuereporter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/memory_info.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/crashdumps_platform.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/throwsite.h
)

# R06b: crash records and dumps (Windows minidumps through dbghelp) and throw-site stacks. On Linux
# and macOS throwsite_posix.cpp defines __cxa_throw; it is linked because crashdumps.cpp uses its
# functions.
if(WIN32)
  target_sources(klogg_crash_handler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/crashdumps_windows.cpp)
  target_link_libraries(klogg_crash_handler PUBLIC dbghelp shell32)
else()
  target_sources(klogg_crash_handler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/crashdumps_posix.cpp
                                             ${CMAKE_CURRENT_SOURCE_DIR}/src/throwsite_posix.cpp)
  target_link_libraries(klogg_crash_handler PUBLIC ${CMAKE_DL_LIBS})
endif()

set_target_properties(klogg_crash_handler PROPERTIES AUTOMOC ON)

target_include_directories(klogg_crash_handler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_crash_handler
  PUBLIC project_options
         project_warnings
         klogg_version
         klogg_utils
         klogg_logging
         klogg_karchive
         tbb
         Qt${QT_VERSION_MAJOR}::Gui
         Qt${QT_VERSION_MAJOR}::Widgets
)

if(KLOGG_USE_SENTRY)
  target_compile_definitions(klogg_crash_handler PUBLIC -DKLOGG_USE_SENTRY)
  target_sources(klogg_crash_handler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/crashhandler.cpp)
  target_link_libraries(klogg_crash_handler PUBLIC sentry crashpad_client)
endif()

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_crash_handler PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
