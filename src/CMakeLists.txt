include(FetchContent)

# Each package has its own include root, `<package>/`, under which its headers
# are `cursiv/<package>/...`: a target reads only the packages it links.

# Core, the base of every other package.
file(GLOB_RECURSE CURSIV_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/core/cursiv/*.cpp)
add_library(cursivCore STATIC ${CURSIV_CORE_SOURCES})
add_library(cursiv::core ALIAS cursivCore)
target_include_directories(cursivCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>)
target_compile_features(cursivCore PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(cursivCore PUBLIC Threads::Threads)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CursivAllocator.cmake)

# The parser runtimes, what they share first. A generated language links the
# runtime of its engine.
file(GLOB_RECURSE CURSIV_ENGINE_SHARED_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/engines/cursiv/shared/runtime/*.cpp)
add_library(cursivEngineShared STATIC ${CURSIV_ENGINE_SHARED_SOURCES})
add_library(cursiv::engine::shared ALIAS cursivEngineShared)
target_include_directories(cursivEngineShared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engines>)
target_link_libraries(cursivEngineShared PUBLIC cursiv::core)

add_library(cursivEngineLl INTERFACE)
add_library(cursiv::engine::ll ALIAS cursivEngineLl)
target_include_directories(cursivEngineLl INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engines>)
target_link_libraries(cursivEngineLl INTERFACE cursiv::engine::shared)

file(GLOB_RECURSE CURSIV_ENGINE_LR_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/engines/cursiv/lr/runtime/*.cpp)
add_library(cursivEngineLr STATIC ${CURSIV_ENGINE_LR_SOURCES})
add_library(cursiv::engine::lr ALIAS cursivEngineLr)
target_include_directories(cursivEngineLr PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engines>)
target_link_libraries(cursivEngineLr PUBLIC cursiv::engine::shared)

# Shared generator support and parser backends.
file(GLOB CURSIV_GEN_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/engines/cursiv/shared/gen/*.cpp)
add_library(cursivGen STATIC EXCLUDE_FROM_ALL ${CURSIV_GEN_SOURCES})
add_library(cursiv::gen ALIAS cursivGen)
target_link_libraries(cursivGen PUBLIC cursiv::engine::shared)

foreach(_backend IN ITEMS Ll Lr)
  string(TOLOWER ${_backend} _engine)
  file(GLOB _sources CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/engines/cursiv/${_engine}/gen/*.cpp)
  list(REMOVE_ITEM _sources
       ${CMAKE_CURRENT_SOURCE_DIR}/engines/cursiv/${_engine}/gen/${_engine}_main.cpp)
  add_library(cursiv${_backend}Gen STATIC EXCLUDE_FROM_ALL ${_sources})
  add_library(cursiv::${_engine}_gen ALIAS cursiv${_backend}Gen)
  target_link_libraries(cursiv${_backend}Gen PUBLIC cursiv::gen
      cursiv::engine::${_engine})
  # These large emitters only write source files. Keep grammar analysis and
  # parser runtimes at the configured optimisation level.
  set_property(SOURCE engines/cursiv/${_engine}/gen/${_engine}_emit_parser.cpp
      APPEND PROPERTY COMPILE_OPTIONS $<$<CONFIG:Release>:-O1>)
endforeach()

# Protocol types, JSON-RPC and transports from our pinned lsp-framework fork.
set(LSP_BUILD_EXAMPLES OFF CACHE BOOL "")
set(LSP_INSTALL OFF CACHE BOOL "")
FetchContent_Declare(
    lsp_framework
    GIT_REPOSITORY https://github.com/ydaveluy/lsp-framework.git
    GIT_TAG        00162a2b96fc7474f2c76e270449b5e7f59c46b8
    GIT_SUBMODULES ""
    EXCLUDE_FROM_ALL)
block(PROPAGATE lsp_framework_SOURCE_DIR lsp_framework_BINARY_DIR)
  set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
  FetchContent_MakeAvailable(lsp_framework)
endblock()
# Native host builds need only generators and their dependencies.
if(CURSIV_HOST_MANIFEST)
  return()
endif()

# Command-line and editor services.
file(GLOB CURSIV_CLI_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cli/cursiv/*.cpp)
add_library(cursivCli STATIC EXCLUDE_FROM_ALL ${CURSIV_CLI_SOURCES})
add_library(cursiv::cli ALIAS cursivCli)
target_include_directories(cursivCli PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cli>)
target_link_libraries(cursivCli PUBLIC cursiv::core)

file(GLOB_RECURSE CURSIV_MCP_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/mcp/cursiv/*.cpp)
add_library(cursivMcp STATIC EXCLUDE_FROM_ALL ${CURSIV_MCP_SOURCES})
add_library(cursiv::mcp ALIAS cursivMcp)
target_include_directories(cursivMcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mcp>)
target_link_libraries(cursivMcp PUBLIC cursiv::core lsp)

file(GLOB_RECURSE CURSIV_LSP_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/lsp/cursiv/*.cpp)
add_library(cursivLsp STATIC EXCLUDE_FROM_ALL ${CURSIV_LSP_SOURCES})
add_library(cursiv::lsp ALIAS cursivLsp)
target_include_directories(cursivLsp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lsp>)
# Public headers expose protocol types.
target_link_libraries(cursivLsp PUBLIC cursiv::core cursiv::mcp lsp)

# Test and benchmark helpers are available to language projects too.
file(GLOB CURSIV_TEST_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/test/cursiv/*.cpp)
add_library(cursivTest STATIC EXCLUDE_FROM_ALL ${CURSIV_TEST_SOURCES})
add_library(cursiv::test ALIAS cursivTest)
# The fuzz helpers are headers on top of the test helpers.
target_include_directories(cursivTest PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fuzz>)
# Core-only tests should not inherit LSP dependencies.
target_link_libraries(cursivTest PUBLIC cursiv::engine::shared PRIVATE cursiv::lsp)

add_library(cursivBench STATIC EXCLUDE_FROM_ALL
    bench/cursiv/LanguageBench.cpp bench/cursiv/LspBench.cpp)
add_library(cursiv::bench ALIAS cursivBench)
target_include_directories(cursivBench PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bench>)
target_link_libraries(cursivBench PUBLIC cursiv::lsp)
if(CMAKE_CROSSCOMPILING AND NOT CURSIV_WASI_THREADS)
  target_compile_definitions(cursivBench PUBLIC CURSIV_BENCH_PARSE_ONLY)
endif()
