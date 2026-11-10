# Core: the language itself. Generates the parser and the editor's keywords
# from the grammar, then builds the services and checks under core/.
cursiv_generate_language(ID         statemachine
    GRAMMAR    statemachine/StateMachineGrammar.hpp
    NAMESPACE  sm::SM
    EXTENSIONS .statemachine
    OUT        statemachine/generated
    TEXTMATE   ../vscode/syntaxes/statemachine.tmLanguage.json)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(statemachineCore ${_core})
target_link_libraries(statemachineCore PUBLIC statemachineGenerated)

# CLI: the command line. cli.cpp starts the executable; the code under cli/
# builds into a library that the tests can link.
file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp)
add_library(statemachineCli ${_cli})
target_include_directories(statemachineCli PUBLIC cli)
target_link_libraries(statemachineCli PUBLIC statemachineCore cursiv::cli)
add_executable(statemachine_cli cli.cpp)
target_link_libraries(statemachine_cli PRIVATE statemachineCli)

# LSP: the language server. lsp.cpp starts the executable; the code under lsp/
# builds into a library that the tests and the benchmark can link.
file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/lsp/*.cpp)
add_library(statemachineLsp ${_lsp})
target_include_directories(statemachineLsp PUBLIC lsp)
target_link_libraries(statemachineLsp PUBLIC statemachineCore cursiv::lsp)
add_executable(statemachine_lsp lsp.cpp)
target_link_libraries(statemachine_lsp PRIVATE statemachineLsp)

# VS Code: a WASI build copies the language server into vscode/server/.
cursiv_package_language(statemachine)
