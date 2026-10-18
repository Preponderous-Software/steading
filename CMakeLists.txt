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

add_executable(steading-sim app/sim.cpp)
target_link_libraries(steading-sim PRIVATE steading_colony)
steading_warnings(steading-sim)

if(STEADING_BUILD_TESTS)
  enable_testing()
  set(STEADING_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(t generate systems determinism mods)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE steading_colony steading_mods)
    target_compile_definitions(test_${t} PRIVATE STEADING_SOURCE_DIR="${STEADING_SOURCE_DIR}")
    steading_warnings(test_${t})
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
endif()
