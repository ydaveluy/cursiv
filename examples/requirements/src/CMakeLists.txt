# Core: the two languages. Generates a parser for each from the grammar file,
# then builds the services and checks under core/. Every other package links it.
cursiv_generate_language(ID         requirements-lang
    GRAMMAR    requirements/RequirementsGrammar.hpp
    NAMESPACE  req::Requirements
    EXTENSIONS .req
    OUT        requirements/generated)

cursiv_generate_language(ID         tests-lang
    GRAMMAR    requirements/RequirementsGrammar.hpp
    NAMESPACE  req::Tests
    EXTENSIONS .tst
    OUT        requirements/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(requirementsCore ${_core})
target_link_libraries(requirementsCore PUBLIC requirements-langGenerated tests-langGenerated)

# CLI: the command line. cli.cpp starts the executable; the code under cli/
# builds into a library that the tests can link.
file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp)
add_library(requirementsCli ${_cli})
target_include_directories(requirementsCli PUBLIC cli)
target_link_libraries(requirementsCli PUBLIC requirementsCore cursiv::cli)
add_executable(requirements_cli cli.cpp)
target_link_libraries(requirements_cli PRIVATE requirementsCli)

# LSP: the language server. lsp.cpp starts the executable; the code under lsp/
# builds into a library that the tests and the benchmark can link.
file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/lsp/*.cpp)
add_library(requirementsLsp ${_lsp})
target_include_directories(requirementsLsp PUBLIC lsp)
target_link_libraries(requirementsLsp PUBLIC requirementsCore cursiv::lsp)
add_executable(requirements_lsp lsp.cpp)
target_link_libraries(requirements_lsp PRIVATE requirementsLsp)

# VS Code: a WASI build copies the language server into vscode/server/.
cursiv_package_language(requirements)
