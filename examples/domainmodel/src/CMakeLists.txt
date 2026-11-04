# Core: the language itself. Generates the parser from the grammar, then builds
# the services and checks under core/. Every other package links it.
cursiv_generate_language(ID         domain-model
    GRAMMAR    domainmodel/DomainModelGrammar.hpp
    NAMESPACE  dm::DomainModel
    EXTENSIONS .dmodel
    OUT        domainmodel/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(domainmodelCore ${_core})
target_link_libraries(domainmodelCore PUBLIC domain-modelGenerated)

# CLI: the command line. cli.cpp starts the executable; the code under cli/
# builds into a library that the tests can link.
file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp)
add_library(domainmodelCli ${_cli})
target_include_directories(domainmodelCli PUBLIC cli)
target_link_libraries(domainmodelCli PUBLIC domainmodelCore cursiv::cli)
add_executable(domainmodel_cli cli.cpp)
target_link_libraries(domainmodel_cli PRIVATE domainmodelCli)

# LSP: the language server. lsp.cpp starts the executable; the code under lsp/
# builds into a library that the tests and the benchmark can link.
file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/lsp/*.cpp)
add_library(domainmodelLsp ${_lsp})
target_include_directories(domainmodelLsp PUBLIC lsp)
target_link_libraries(domainmodelLsp PUBLIC domainmodelCore cursiv::lsp)
add_executable(domainmodel_lsp lsp.cpp)
target_link_libraries(domainmodel_lsp PRIVATE domainmodelLsp)

# VS Code: a WASI build copies the language server into vscode/server/.
cursiv_package_language(domainmodel)
