include(FetchContent)

# Core and parser runtime, shared by all generated languages.
file(GLOB_RECURSE CURSIV_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/core/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/engines/shared/runtime/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/engines/lr/runtime/*.cpp)
add_library(cursivCore STATIC ${CURSIV_CORE_SOURCES})
add_library(cursiv::core ALIAS cursivCore)
target_include_directories(cursivCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(cursivCore PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(cursivCore PUBLIC Threads::Threads)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CursivAllocator.cmake)

# Shared generator support and parser backends.
file(GLOB CURSIV_GEN_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/engines/shared/gen/*.cpp)
add_library(cursivGen STATIC EXCLUDE_FROM_ALL ${CURSIV_GEN_SOURCES})
add_library(cursiv::gen ALIAS cursivGen)
target_link_libraries(cursivGen PUBLIC cursiv::core)

foreach(_backend IN ITEMS Ll Lr)
  string(TOLOWER ${_backend} _engine)
  file(GLOB _sources CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/engines/${_engine}/gen/*.cpp)
  list(REMOVE_ITEM _sources
       ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/engines/${_engine}/gen/${_engine}_main.cpp)
  add_library(cursiv${_backend}Gen STATIC EXCLUDE_FROM_ALL ${_sources})
  add_library(cursiv::${_engine}_gen ALIAS cursiv${_backend}Gen)
  target_link_libraries(cursiv${_backend}Gen PUBLIC cursiv::core cursiv::gen)
endforeach()

# Protocol types, JSON-RPC and transports from our pinned lsp-framework fork.
set(LSP_BUILD_EXAMPLES OFF CACHE BOOL "")
set(LSP_INSTALL OFF CACHE BOOL "")
FetchContent_Declare(
    lsp_framework
    GIT_REPOSITORY https://github.com/ydaveluy/lsp-framework.git
    GIT_TAG        35df0dab18bd266dba22903900ea2f36f6d49f24
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
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/cli/*.cpp)
add_library(cursivCli STATIC EXCLUDE_FROM_ALL ${CURSIV_CLI_SOURCES})
add_library(cursiv::cli ALIAS cursivCli)
target_link_libraries(cursivCli PUBLIC cursiv::core)

file(GLOB_RECURSE CURSIV_MCP_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/mcp/*.cpp)
add_library(cursivMcp STATIC EXCLUDE_FROM_ALL ${CURSIV_MCP_SOURCES})
add_library(cursiv::mcp ALIAS cursivMcp)
target_link_libraries(cursivMcp PUBLIC cursiv::core lsp)

file(GLOB_RECURSE CURSIV_LSP_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/lsp/*.cpp)
add_library(cursivLsp STATIC EXCLUDE_FROM_ALL ${CURSIV_LSP_SOURCES})
add_library(cursiv::lsp ALIAS cursivLsp)
# Public headers expose protocol types.
target_link_libraries(cursivLsp PUBLIC cursiv::core cursiv::mcp lsp)

# Test and benchmark helpers are available to language projects too.
file(GLOB CURSIV_TEST_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cursiv/test/*.cpp)
add_library(cursivTest STATIC EXCLUDE_FROM_ALL ${CURSIV_TEST_SOURCES})
add_library(cursiv::test ALIAS cursivTest)
# Core-only tests should not inherit LSP dependencies.
target_link_libraries(cursivTest PUBLIC cursiv::core PRIVATE cursiv::lsp)

add_library(cursivBench STATIC EXCLUDE_FROM_ALL
    cursiv/bench/LanguageBench.cpp cursiv/bench/LspBench.cpp)
add_library(cursiv::bench ALIAS cursivBench)
target_link_libraries(cursivBench PUBLIC cursiv::lsp)
if(CMAKE_CROSSCOMPILING AND NOT CURSIV_WASI_THREADS)
  target_compile_definitions(cursivBench PUBLIC CURSIV_BENCH_PARSE_ONLY)
endif()
