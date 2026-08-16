cmake_minimum_required(VERSION 3.20)
project(eaw_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MINGW)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

# Portable engine core — builds on Windows AND Linux natively
add_library(eaw_core STATIC
    core/meg_file.cpp
    core/xml.cpp
    core/game_constants.cpp
)
target_include_directories(eaw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(eaw_core PUBLIC cxx_std_20)

# CLI tools
add_executable(meg_tool
    cli/meg_tool.cpp
)
target_link_libraries(meg_tool PRIVATE eaw_core)

add_executable(gameconfig_tool
    cli/gameconfig_tool.cpp
)
target_link_libraries(gameconfig_tool PRIVATE eaw_core)

# Tests
enable_testing()
add_executable(meg_tests
    tests/test_meg.cpp
)
target_link_libraries(meg_tests PRIVATE eaw_core)
add_test(NAME meg_tests COMMAND meg_tests)

add_executable(xml_tests
    tests/test_xml.cpp
)
target_link_libraries(xml_tests PRIVATE eaw_core)
add_test(NAME xml_tests COMMAND xml_tests)
