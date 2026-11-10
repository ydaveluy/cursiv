# Core: the language itself. Generates the parser and the editor's keywords
# from the grammar, then builds the services and checks under core/.
cursiv_generate_language(ID         arithmetics
    GRAMMAR    arithmetics/ArithmeticsGrammar.hpp
    NAMESPACE  arith::Arith
    EXTENSIONS .calc
    OUT        arithmetics/generated
    TEXTMATE   ../vscode/syntaxes/arithmetics.tmLanguage.json)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(arithmeticsCore ${_core})
target_link_libraries(arithmeticsCore PUBLIC arithmeticsGenerated)

# CLI: the command line. cli.cpp starts the executable; the code under cli/
# builds into a library that the tests can link.
file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp)
add_library(arithmeticsCli ${_cli})
target_include_directories(arithmeticsCli PUBLIC cli)
target_link_libraries(arithmeticsCli PUBLIC arithmeticsCore cursiv::cli)
add_executable(arithmetics_cli cli.cpp)
target_link_libraries(arithmetics_cli PRIVATE arithmeticsCli)

# LSP: the language server. lsp.cpp starts the executable; the code under lsp/
# builds into a library that the tests and the benchmark can link.
file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/lsp/*.cpp)
add_library(arithmeticsLsp ${_lsp})
target_include_directories(arithmeticsLsp PUBLIC lsp)
target_link_libraries(arithmeticsLsp PUBLIC arithmeticsCore cursiv::lsp)
add_executable(arithmetics_lsp lsp.cpp)
target_link_libraries(arithmetics_lsp PRIVATE arithmeticsLsp)

# VS Code: a WASI build copies the language server into vscode/server/.
cursiv_package_language(arithmetics)
