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
    core/event_system.cpp
    core/path_grid.cpp
    core/pathfinding.cpp
    core/perception.cpp
    core/ai_targeting.cpp
    core/ai_driver.cpp
    core/unit_data_loader.cpp
    core/pg_bindings.cpp
    core/pg_object_bindings.cpp
    core/pg_event_bindings.cpp
    core/pg_taskforce_bindings.cpp
    core/pg_fog_bindings.cpp
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

add_executable(sim_tool
    cli/sim_tool.cpp
)
target_link_libraries(sim_tool PRIVATE eaw_core)

add_executable(mod_tool
    cli/mod_tool.cpp
)
target_link_libraries(mod_tool PRIVATE eaw_core)

# Patch DLL (Tier 3): d3d9 proxy that hooks the game's sim tick.
add_subdirectory(../third_party/minhook ${CMAKE_BINARY_DIR}/minhook)
add_library(patch_d3d9 SHARED
    proxy/d3d9.cpp
    proxy/d3d9.def
)
set_target_properties(patch_d3d9 PROPERTIES PREFIX "" OUTPUT_NAME "d3d9")
target_link_libraries(patch_d3d9 PRIVATE minhook)
target_include_directories(patch_d3d9 PRIVATE ../third_party/minhook/include)
# MinHook's x64 trampolines must not use the red zone.
if(MINGW)
    target_compile_options(patch_d3d9 PRIVATE -mno-red-zone)
endif()

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

add_executable(event_system_tests
    tests/test_event_system.cpp
)
target_link_libraries(event_system_tests PRIVATE eaw_core)
add_test(NAME event_system_tests COMMAND event_system_tests)

add_executable(combat_tests
    tests/test_combat.cpp
)
target_link_libraries(combat_tests PRIVATE eaw_core)
add_test(NAME combat_tests COMMAND combat_tests)

add_executable(pathfinding_tests
    tests/test_pathfinding.cpp
)
target_link_libraries(pathfinding_tests PRIVATE eaw_core)
add_test(NAME pathfinding_tests COMMAND pathfinding_tests)

add_executable(taskforce_tests
    tests/test_taskforce.cpp
)
target_link_libraries(taskforce_tests PRIVATE eaw_core)
add_test(NAME taskforce_tests COMMAND taskforce_tests)

add_executable(unit_data_tests
    tests/test_unit_data_loader.cpp
)
target_link_libraries(unit_data_tests PRIVATE eaw_core)
add_test(NAME unit_data_tests COMMAND unit_data_tests)

add_executable(perception_tests
    tests/test_perception.cpp
)
target_link_libraries(perception_tests PRIVATE eaw_core)
add_test(NAME perception_tests COMMAND perception_tests)

add_executable(ai_tests
    tests/test_ai.cpp
)
target_link_libraries(ai_tests PRIVATE eaw_core)
add_test(NAME ai_tests COMMAND ai_tests)

add_executable(economy_tests
    tests/test_economy.cpp
)
target_link_libraries(economy_tests PRIVATE eaw_core)
add_test(NAME economy_tests COMMAND economy_tests)

add_executable(mod_compat_tests
    tests/test_mod_compat.cpp
)
target_link_libraries(mod_compat_tests PRIVATE eaw_core)
add_test(NAME mod_compat_tests COMMAND mod_compat_tests)

add_executable(ability_tests
    tests/test_abilities.cpp
)
target_link_libraries(ability_tests PRIVATE eaw_core)
add_test(NAME ability_tests COMMAND ability_tests)

add_executable(fog_tests
    tests/test_fog.cpp
)
target_link_libraries(fog_tests PRIVATE eaw_core)
add_test(NAME fog_tests COMMAND fog_tests)

add_executable(ai_plan_tests
    tests/test_ai_plans.cpp
)
target_link_libraries(ai_plan_tests PRIVATE eaw_core)
add_test(NAME ai_plan_tests COMMAND ai_plan_tests)

# Real-game smoke test (needs the game install; run manually, not in ctest)
add_executable(real_lua_test
    tests/test_real_lua.cpp
)
target_link_libraries(real_lua_test PRIVATE eaw_core)
