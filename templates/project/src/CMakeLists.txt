# Core: the language itself. Generates the parser and the editor's keywords
# from the grammar, again when it changes, then builds the services and checks
# under core/. Every other package links it.
cursiv_generate_language(ID @CURSIV_NEW_ID@
    GRAMMAR    @CURSIV_NEW_ID@/@CURSIV_NEW_CLASS@Grammar.hpp
    NAMESPACE  @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@
    EXTENSIONS @CURSIV_NEW_EXT@
    OUT        @CURSIV_NEW_ID@/generated
    TEXTMATE   ../vscode/syntaxes/@CURSIV_NEW_ID@.tmLanguage.json)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(@CURSIV_NEW_ID@Core ${_core})
target_link_libraries(@CURSIV_NEW_ID@Core PUBLIC @CURSIV_NEW_ID@Generated)

# CLI: the command line. cli.cpp starts the executable; the code under cli/
# builds into a library that the tests can link. Delete cli.cpp to drop it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli.cpp")
  add_executable(@CURSIV_NEW_ID@_cli cli.cpp)
  target_include_directories(@CURSIV_NEW_ID@_cli PRIVATE cli)
  target_link_libraries(@CURSIV_NEW_ID@_cli PRIVATE @CURSIV_NEW_ID@Core cursiv::cli)
  file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp)
  if(_cli)
    add_library(@CURSIV_NEW_ID@Cli ${_cli})
    target_include_directories(@CURSIV_NEW_ID@Cli PUBLIC cli)
    target_link_libraries(@CURSIV_NEW_ID@Cli PUBLIC @CURSIV_NEW_ID@Core cursiv::cli)
    target_link_libraries(@CURSIV_NEW_ID@_cli PRIVATE @CURSIV_NEW_ID@Cli)
  endif()
endif()

# LSP: the language server. lsp.cpp starts the executable; the code under lsp/
# builds into a library that the tests and the benchmark can link. Delete lsp.cpp
# and lsp/ to drop it.
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lsp")
  file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/lsp/*.cpp)
  add_library(@CURSIV_NEW_ID@Lsp ${_lsp})
  target_include_directories(@CURSIV_NEW_ID@Lsp PUBLIC lsp)
  target_link_libraries(@CURSIV_NEW_ID@Lsp PUBLIC @CURSIV_NEW_ID@Core cursiv::lsp)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lsp.cpp")
    add_executable(@CURSIV_NEW_ID@_lsp lsp.cpp)
    target_link_libraries(@CURSIV_NEW_ID@_lsp PRIVATE @CURSIV_NEW_ID@Lsp)
  endif()
endif()

# Benchmark: bench.cpp measures the language through the LSP library. Delete it
# to drop it.
if(TARGET @CURSIV_NEW_ID@Lsp AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench.cpp")
  cursiv_add_benchmark(@CURSIV_NEW_ID@ SOURCES bench.cpp)
endif()

# VS Code: a WASI build copies the language server into vscode/server/.
cursiv_package_language(@CURSIV_NEW_ID@)
