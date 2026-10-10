cmake_minimum_required(VERSION 3.20)
project(eclipse_biomass LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ECLIPSE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(eclipse_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    if(ECLIPSE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(ECLIPSE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# The core has no dependency beyond the standard library, so the same sources
# build inside the Unreal plugin.
add_library(eclipse_core STATIC
  core/src/corruption.cpp
  core/src/export.cpp
  core/src/geometry.cpp
  core/src/growths.cpp
  core/src/hosts.cpp
  core/src/mesh.cpp
  core/src/noise.cpp
  core/src/preview.cpp
  core/src/sdf.cpp
  core/src/spatial.cpp
)
target_include_directories(eclipse_core PUBLIC core/include)
eclipse_warnings(eclipse_core)

add_executable(eclipse-cli cli/main.cpp)
target_link_libraries(eclipse-cli PRIVATE eclipse_core)
eclipse_warnings(eclipse-cli)

enable_testing()
add_executable(eclipse_tests tests/tests.cpp)
target_link_libraries(eclipse_tests PRIVATE eclipse_core)
eclipse_warnings(eclipse_tests)
add_test(NAME eclipse_tests COMMAND eclipse_tests)
