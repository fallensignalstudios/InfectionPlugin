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
    # fopen and friends are used deliberately for portability.
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
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

# The core has no dependency beyond the standard library. Its sources live in
# the Unreal plugin's EclipseCore module so there is exactly one copy; this
# project builds them on their own for the CLI and the tests.
add_library(eclipse_core STATIC
  Source/EclipseCore/Private/corruption.cpp
  Source/EclipseCore/Private/export.cpp
  Source/EclipseCore/Private/geometry.cpp
  Source/EclipseCore/Private/growths.cpp
  Source/EclipseCore/Private/hosts.cpp
  Source/EclipseCore/Private/mesh.cpp
  Source/EclipseCore/Private/noise.cpp
  Source/EclipseCore/Private/preview.cpp
  Source/EclipseCore/Private/sdf.cpp
  Source/EclipseCore/Private/spatial.cpp
)
target_include_directories(eclipse_core PUBLIC Source/EclipseCore/Public)
eclipse_warnings(eclipse_core)

add_executable(eclipse-cli cli/main.cpp)
target_link_libraries(eclipse-cli PRIVATE eclipse_core)
eclipse_warnings(eclipse-cli)

enable_testing()
add_executable(eclipse_tests tests/tests.cpp)
target_link_libraries(eclipse_tests PRIVATE eclipse_core)
eclipse_warnings(eclipse_tests)
add_test(NAME eclipse_tests COMMAND eclipse_tests)
