cmake_minimum_required(VERSION 3.30)
project(cursiv LANGUAGES CXX)
# Reject cross-build options on native configurations.
if(DEFINED CURSIV_WASI_THREADS AND NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR
      "CURSIV_WASI_THREADS selects the wasi toolchain's threaded target: "
      "configure with `cmake --preset wasi-release`")
endif()

option(CURSIV_BUILD_TESTS "Build Cursiv's tests" ${PROJECT_IS_TOP_LEVEL})
option(CURSIV_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors"
    ${PROJECT_IS_TOP_LEVEL})

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED CMAKE_CXX_EXTENSIONS)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(BUILD_SHARED_LIBS AND NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()
if(CURSIV_WARNINGS_AS_ERRORS)
  set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif()

find_program(CURSIV_CCACHE ccache)
if(CURSIV_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CURSIV_CCACHE})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND CURSIV_NODE)
  set(CMAKE_CROSSCOMPILING_EMULATOR
      ${CURSIV_NODE} --no-warnings
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/wasi-run.mjs)
endif()

add_subdirectory(src)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CursivGrammar.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CursivBenchmark.cmake)

# Cross builds configure a native tree containing only their generators.
if(CURSIV_HOST_MANIFEST)
  include(${CURSIV_HOST_MANIFEST})
  return()
endif()

# Enable testing before entering examples, which declare tests too.
if(CURSIV_BUILD_TESTS)
  enable_testing()
endif()

# Tests use example languages; benchmarks use test fixtures.
if(PROJECT_IS_TOP_LEVEL OR CURSIV_BUILD_TESTS)
  add_subdirectory(examples)
endif()

if(CURSIV_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(PROJECT_IS_TOP_LEVEL)
  add_subdirectory(bench)
endif()
