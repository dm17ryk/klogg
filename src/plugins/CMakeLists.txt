add_library(klogg_plugins STATIC
  include/pluginmanifest.h src/pluginmanifest.cpp
  include/plugincatalog.h src/plugincatalog.cpp
  include/pluginprocess.h src/pluginprocess.cpp
  include/pluginrepositories.h src/pluginrepositories.cpp
  include/pluginrepository.h src/pluginrepository.cpp
  include/pluginstorage.h src/pluginstorage.cpp
  include/pluginmanager.h src/pluginmanager.cpp src/pluginmanagerrepositories.cpp
  include/pythonruntime.h src/pythonruntime.cpp
  include/semanticversion.h src/semanticversion.cpp)
set_target_properties(klogg_plugins PROPERTIES AUTOMOC ON)
target_include_directories(klogg_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(klogg_plugins PUBLIC project_options project_warnings
  klogg_logging klogg_settings klogg_karchive
  Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Concurrent
  PRIVATE klogg_version)
