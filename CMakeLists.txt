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
# The Strata commit the game is built against. Pinned; bump deliberately.
set(STEADING_STRATA_REF "f3e44a2e23666c96d7acf458a84c1b4af70aa312" CACHE STRING "Strata git ref (a full commit SHA in CI)")

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

if(STEADING_BUILD_TESTS)
  enable_testing()
  set(STEADING_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(t generate systems determinism mods replay)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE steading_colony steading_mods steading_replay)
    target_compile_definitions(test_${t} PRIVATE STEADING_SOURCE_DIR="${STEADING_SOURCE_DIR}")
    steading_warnings(test_${t})
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
  endif()
endif()
