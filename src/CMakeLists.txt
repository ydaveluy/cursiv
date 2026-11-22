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

# The parser engine. A generated language links its runtime; a language's
# generator links what builds a parser, which the runtime is no part of.
set(_engine ${CMAKE_CURRENT_SOURCE_DIR}/engine/cursiv/engine)
add_library(cursivEngine STATIC
    ${_engine}/ast/AstBuilder.cpp
    ${_engine}/completion/Caret.cpp
    ${_engine}/parser/LanguageParser.cpp
    ${_engine}/parser/Match.cpp
    ${_engine}/parser/Parse.cpp
    ${_engine}/parser/Split.cpp
    ${_engine}/recovery/Repair.cpp
    ${_engine}/recovery/Typos.cpp
    ${_engine}/recovery/Unclosed.cpp)
add_library(cursiv::engine ALIAS cursivEngine)
target_include_directories(cursivEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine>)
target_link_libraries(cursivEngine PUBLIC cursiv::core)

add_library(cursivGen STATIC EXCLUDE_FROM_ALL
    ${_engine}/ast/EmitAst.cpp
    ${_engine}/generator/Generate.cpp
    ${_engine}/generator/GrammarPrinter.cpp
    ${_engine}/generator/TextMate.cpp
    ${_engine}/grammar/GrammarImage.cpp
    ${_engine}/grammar/Lower.cpp
    ${_engine}/grammar/WritesOnce.cpp
    ${_engine}/lexer/CharClass.cpp
    ${_engine}/lexer/LexAutomaton.cpp
    ${_engine}/parser/Closers.cpp
    ${_engine}/parser/EmitDrive.cpp
    ${_engine}/parser/InsertOrder.cpp
    ${_engine}/parser/Lr1.cpp
    ${_engine}/parser/Pack.cpp)
add_library(cursiv::gen ALIAS cursivGen)
target_include_directories(cursivGen PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine>)
target_link_libraries(cursivGen PUBLIC cursiv::core)
# The emitter only writes source files: the grammar's analysis keeps the
# configured optimisation level.
set_property(SOURCE ${_engine}/parser/EmitDrive.cpp
    APPEND PROPERTY COMPILE_OPTIONS $<$<CONFIG:Release>:-O1>)

# Protocol types, JSON-RPC and transports from our pinned lsp-framework fork.
set(LSP_BUILD_EXAMPLES OFF CACHE BOOL "")
set(LSP_INSTALL OFF CACHE BOOL "")
FetchContent_Declare(
    lsp_framework
    GIT_REPOSITORY https://github.com/ydaveluy/lsp-framework.git
    GIT_TAG        843219fef6cf0713ef33fcad335212419969d423
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
target_link_libraries(cursivTest PUBLIC cursiv::engine PRIVATE cursiv::lsp)

add_library(cursivBench STATIC EXCLUDE_FROM_ALL
    bench/cursiv/LanguageBench.cpp bench/cursiv/LspBench.cpp)
add_library(cursiv::bench ALIAS cursivBench)
target_include_directories(cursivBench PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bench>)
target_link_libraries(cursivBench PUBLIC cursiv::lsp)
if(CMAKE_CROSSCOMPILING AND NOT CURSIV_WASI_THREADS)
  target_compile_definitions(cursivBench PUBLIC CURSIV_BENCH_PARSE_ONLY)
endif()
