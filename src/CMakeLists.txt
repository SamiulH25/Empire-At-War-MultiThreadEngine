cmake_minimum_required(VERSION 3.20)
project(eaw_engine C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vendored Lua
add_subdirectory(../third_party/lua ${CMAKE_BINARY_DIR}/lua)

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
    core/job_system.cpp
    core/lua_host.cpp
    core/meg_manager.cpp
    core/object_model.cpp
    core/pg_bindings.cpp
    core/pg_object_bindings.cpp
    core/script_manager.cpp
    core/simulation.cpp
)
target_include_directories(eaw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(eaw_core PUBLIC cxx_std_20)
target_link_libraries(eaw_core PUBLIC lua)

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

add_executable(meg_manager_tests
    tests/test_meg_manager.cpp
)
target_link_libraries(meg_manager_tests PRIVATE eaw_core)
add_test(NAME meg_manager_tests COMMAND meg_manager_tests)

add_executable(xml_tests
    tests/test_xml.cpp
)
target_link_libraries(xml_tests PRIVATE eaw_core)
add_test(NAME xml_tests COMMAND xml_tests)

add_executable(job_system_tests
    tests/test_job_system.cpp
)
target_link_libraries(job_system_tests PRIVATE eaw_core)
add_test(NAME job_system_tests COMMAND job_system_tests)

add_executable(lua_tests
    tests/test_lua.cpp
)
target_link_libraries(lua_tests PRIVATE eaw_core)
add_test(NAME lua_tests COMMAND lua_tests)

add_executable(pg_bindings_tests
    tests/test_pg_bindings.cpp
)
target_link_libraries(pg_bindings_tests PRIVATE eaw_core)
add_test(NAME pg_bindings_tests COMMAND pg_bindings_tests)

add_executable(script_manager_tests
    tests/test_script_manager.cpp
)
target_link_libraries(script_manager_tests PRIVATE eaw_core)
add_test(NAME script_manager_tests COMMAND script_manager_tests)

add_executable(object_bindings_tests
    tests/test_object_bindings.cpp
)
target_link_libraries(object_bindings_tests PRIVATE eaw_core)
add_test(NAME object_bindings_tests COMMAND object_bindings_tests)

add_executable(simulation_tests
    tests/test_simulation.cpp
)
target_link_libraries(simulation_tests PRIVATE eaw_core)
add_test(NAME simulation_tests COMMAND simulation_tests)

# Real-game smoke test (needs the game install; run manually, not in ctest)
add_executable(real_lua_test
    tests/test_real_lua.cpp
)
target_link_libraries(real_lua_test PRIVATE eaw_core)
