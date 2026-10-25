cursiv_generate_language(ID         requirements-lang
    GRAMMAR    requirements/core/RequirementsGrammar.hpp
    NAMESPACE  req::Requirements
    EXTENSIONS .req
    OUT        requirements/generated)

cursiv_generate_language(ID         tests-lang
    GRAMMAR    requirements/core/RequirementsGrammar.hpp
    NAMESPACE  req::Tests
    EXTENSIONS .tst
    OUT        requirements/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/requirements/core/*.cpp)
add_library(requirementsCore ${_core})
target_link_libraries(requirementsCore PUBLIC requirements-langGenerated tests-langGenerated)

file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/requirements/cli/*.cpp)
add_executable(requirements_cli ${_cli})
target_link_libraries(requirements_cli PRIVATE requirementsCore cursiv::cli)

file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/requirements/lsp/*.cpp)
list(REMOVE_ITEM _lsp ${CMAKE_CURRENT_SOURCE_DIR}/requirements/lsp/main.cpp)
add_library(requirementsLsp ${_lsp})
target_link_libraries(requirementsLsp PUBLIC requirementsCore cursiv::lsp)
add_executable(requirements_lsp requirements/lsp/main.cpp)
target_link_libraries(requirements_lsp PRIVATE requirementsLsp)

cursiv_package_language(requirements)
