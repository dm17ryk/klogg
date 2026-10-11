add_executable(file_write_helper
        file_write_helper.cpp
        file_write_helper.h
)

target_link_libraries(file_write_helper klogg_utils tbb)

add_library(test_utils INTERFACE)
target_include_directories(test_utils SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(plugin_fixture plugin_fixture.cpp)
target_include_directories(plugin_fixture PRIVATE ${CMAKE_SOURCE_DIR}/plugins/sdk/cpp)
target_link_libraries(plugin_fixture PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_compile_features(plugin_fixture PRIVATE cxx_std_17)
