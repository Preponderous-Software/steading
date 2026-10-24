cmake_minimum_required(VERSION 3.16)
project(steading VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(STEADING_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(STEADING_BUILD_TESTS "Build the test suite" ON)
option(STEADING_WITH_STRATA "Build the playable game and save integration on the Strata engine (needs OpenUSD)" OFF)
option(STEADING_WEB "Build the browser game (Emscripten + Strata's WebGL 2 backend; no OpenUSD, no saves)" OFF)
# The Strata commit the game is built against (the v0.6.0 tag; strata.ref
# carries the same SHA for CI). Pinned; bump deliberately.
set(STEADING_STRATA_REF "c2d7ee4c1102726c654c90d8361383d9ba3590b3" CACHE STRING "Strata git ref (a full commit SHA in CI)")

# Emscripten: the rules, tools and tests build as they are and the tests run
# under Node; STEADING_WEB adds the browser game. Strata compiles C++
# exceptions as wasm exceptions, and everything in one .wasm must match.
if(EMSCRIPTEN)
  cmake_minimum_required(VERSION 3.24)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>")
  add_link_options(-fwasm-exceptions -sALLOW_MEMORY_GROWTH=1)
  if(STEADING_WITH_STRATA)
    message(FATAL_ERROR "STEADING_WITH_STRATA needs OpenUSD, which is not built for WebAssembly: use STEADING_WEB")
  endif()
endif()
if(STEADING_WEB AND NOT EMSCRIPTEN)
  message(FATAL_ERROR "STEADING_WEB needs Emscripten (emcmake cmake ...)")
endif()

function(steading_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(STEADING_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(STEADING_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# Game rules: components, systems, generation, view projection. USD-free, so
# the rules build and test in seconds without OpenUSD.
add_library(steading_colony STATIC
  src/colony/components.cpp
  src/colony/generate.cpp
  src/colony/systems.cpp
  src/colony/view.cpp)
target_include_directories(steading_colony PUBLIC src)
steading_warnings(steading_colony)
# Determinism: the rules must give bit-identical results on every compiler, so
# never fuse multiply-adds (world generation uses doubles).
if(NOT MSVC)
  target_compile_options(steading_colony PRIVATE -ffp-contract=off)
endif()

# Mod manifests and load-order resolution. USD-free.
add_library(steading_mods STATIC src/mods/manifest.cpp)
target_include_directories(steading_mods PUBLIC src)
steading_warnings(steading_mods)

# History export to a usd-replay/1 replay (usd-recorder's contract), USD-free: the replay is
# written as .usda text by usd-recorder's vendored header (third_party/usd_recorder).
add_library(steading_replay STATIC src/replay/replay.cpp)
target_include_directories(steading_replay PUBLIC src)
target_include_directories(steading_replay SYSTEM PRIVATE third_party)
target_link_libraries(steading_replay PUBLIC steading_colony)
steading_warnings(steading_replay)

add_executable(steading-replay app/replay.cpp)
target_link_libraries(steading-replay PRIVATE steading_replay)
steading_warnings(steading-replay)

add_executable(steading-sim app/sim.cpp)
target_link_libraries(steading-sim PRIVATE steading_colony)
steading_warnings(steading-sim)

if(EMSCRIPTEN)
  # Under Node, the command-line tools and tests read and write the real filesystem.
  target_link_options(steading-replay PRIVATE -sNODERAWFS=1)
  target_link_options(steading-sim PRIVATE -sNODERAWFS=1)
endif()

if(STEADING_BUILD_TESTS)
  enable_testing()
  set(STEADING_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(t generate systems determinism mods replay)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE steading_colony steading_mods steading_replay)
    target_compile_definitions(test_${t} PRIVATE STEADING_SOURCE_DIR="${STEADING_SOURCE_DIR}")
    steading_warnings(test_${t})
    if(EMSCRIPTEN)
      target_link_options(test_${t} PRIVATE -sNODERAWFS=1)
    endif()
    add_test(NAME ${t} COMMAND test_${t} "${CMAKE_CURRENT_BINARY_DIR}")
  endforeach()
endif()

# ---------------------------------------------------------------- the game on Strata
# Needs OpenUSD: pass -DCMAKE_PREFIX_PATH=<openusd install>. Strata is fetched at
# STEADING_STRATA_REF (or taken from -DFETCHCONTENT_SOURCE_DIR_STRATA=<checkout>).
if(STEADING_WITH_STRATA)
  cmake_minimum_required(VERSION 3.24)
  include(FetchContent)
  set(STRATA_WITH_USD ON CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(strata
    GIT_REPOSITORY https://github.com/Preponderous-Software/strata.git
    GIT_TAG ${STEADING_STRATA_REF})
  FetchContent_MakeAvailable(strata)

  add_library(steading_bridge STATIC src/bridge/bridge.cpp src/bridge/session.cpp)
  target_include_directories(steading_bridge PUBLIC src)
  target_link_libraries(steading_bridge PUBLIC steading_colony steading_mods strata::save)
  steading_warnings(steading_bridge)
  target_compile_options(steading_bridge PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-conversion>)

  # With saves: steading-replay also takes --slot (loaded from a read-only copy).
  target_link_libraries(steading-replay PRIVATE steading_bridge)
  target_compile_definitions(steading-replay PRIVATE STEADING_REPLAY_WITH_SAVES
                             STEADING_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  add_executable(steading app/main.cpp app/scene.cpp)
  target_link_libraries(steading PRIVATE steading_bridge strata::static)
  target_compile_definitions(steading PRIVATE STEADING_GAME_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  steading_warnings(steading)

  if(STEADING_BUILD_TESTS)
    add_executable(test_saves tests/test_saves.cpp)
    target_link_libraries(test_saves PRIVATE steading_bridge)
    target_compile_definitions(test_saves PRIVATE STEADING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    steading_warnings(test_saves)
    add_test(NAME saves COMMAND test_saves "${CMAKE_CURRENT_BINARY_DIR}/save-test-out")

    # Released saves (tests/fixtures, written by the game itself) keep loading.
    add_executable(test_save_fixtures tests/test_save_fixtures.cpp)
    target_link_libraries(test_save_fixtures PRIVATE steading_bridge)
    target_compile_definitions(test_save_fixtures PRIVATE STEADING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    steading_warnings(test_save_fixtures)
    add_test(NAME save_fixtures COMMAND test_save_fixtures "${CMAKE_CURRENT_BINARY_DIR}/save-fixture-test-out")

    # The browser build's built-in Stats/Looks equal the core content read through USD.
    add_executable(test_core_defaults tests/test_core_defaults.cpp)
    target_link_libraries(test_core_defaults PRIVATE steading_bridge)
    target_compile_definitions(test_core_defaults PRIVATE STEADING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    steading_warnings(test_core_defaults)
    add_test(NAME core_defaults COMMAND test_core_defaults "${CMAKE_CURRENT_BINARY_DIR}/core-defaults-out")
  endif()
endif()

# ---------------------------------------------------------------- the game in a browser
# emcmake cmake -S . -B build-web -DSTEADING_WEB=ON: Strata's WebGL 2 backend,
# without OpenUSD, so without Strata's USD and save modules. The browser game
# has NO saves and core content only (see app/web_main.cpp and docs/WEB.md).
if(STEADING_WEB)
  include(FetchContent)
  set(STRATA_WITH_USD OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_SIM OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(strata
    GIT_REPOSITORY https://github.com/Preponderous-Software/strata.git
    GIT_TAG ${STEADING_STRATA_REF})
  FetchContent_MakeAvailable(strata)

  add_executable(steading-web app/web_main.cpp app/scene.cpp)
  target_link_libraries(steading-web PRIVATE steading_colony strata::static)
  steading_warnings(steading-web)
  set_target_properties(steading-web PROPERTIES OUTPUT_NAME steading SUFFIX ".html")
  target_link_options(steading-web PRIVATE
    -sSTACK_SIZE=1MB
    "-sEXPORTED_FUNCTIONS=_main,_steading_toggle_pause,_steading_toggle_fly,_steading_speed,_steading_set_tool,_steading_hold,_steading_new_colony,_steading_resize,_steading_advance,_steading_fingerprint,_steading_status"
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  set_property(TARGET steading-web APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
endif()
