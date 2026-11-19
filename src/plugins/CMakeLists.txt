add_library(klogg_plugins STATIC
  include/azuredevops.h src/azuredevops.cpp
  include/configpack.h src/configpack.cpp
  include/packcanonical.h src/packcanonical.cpp
  include/packmerge.h src/packmerge.cpp
  include/packstore.h src/packstore.cpp
  include/pluginmanifest.h src/pluginmanifest.cpp
  include/plugincatalog.h src/plugincatalog.cpp
  include/pluginprocess.h src/pluginprocess.cpp
  include/pluginrepositories.h src/pluginrepositories.cpp
  include/pluginrepository.h src/pluginrepository.cpp
  include/pluginstorage.h src/pluginstorage.cpp
  include/pluginmanager.h src/pluginmanager.cpp src/pluginmanagerrepositories.cpp
  src/pluginmanagerpacks.cpp
  src/pluginmanagerupdates.cpp
  include/pluginupdatechecker.h src/pluginupdatechecker.cpp
  include/pythonruntime.h src/pythonruntime.cpp
  include/repositorycredentials.h src/repositorycredentials.cpp
  include/semanticversion.h src/semanticversion.cpp)
set_target_properties(klogg_plugins PROPERTIES AUTOMOC ON)
target_include_directories(klogg_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(klogg_plugins PUBLIC project_options project_warnings
  klogg_logging klogg_settings klogg_karchive
  Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Concurrent
  PRIVATE klogg_version klogg_libgit2)
# Personal access tokens in the operating system credential store when QtKeychain was built.
if(TARGET klogg_qtkeychain)
  target_sources(klogg_plugins PRIVATE src/keychainpatstore.cpp)
  target_link_libraries(klogg_plugins PRIVATE klogg_qtkeychain)
endif()
