cursiv_generate_language(ID         domain-model
    GRAMMAR    domainmodel/core/DomainModelGrammar.hpp
    NAMESPACE  dm::DomainModel
    EXTENSIONS .dmodel
    OUT        domainmodel/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/domainmodel/core/*.cpp)
add_library(domainmodelCore ${_core})
target_link_libraries(domainmodelCore PUBLIC domain-modelGenerated)

file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/domainmodel/cli/*.cpp)
add_executable(domainmodel_cli ${_cli})
target_link_libraries(domainmodel_cli PRIVATE domainmodelCore cursiv::cli)

file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/domainmodel/lsp/*.cpp)
list(REMOVE_ITEM _lsp ${CMAKE_CURRENT_SOURCE_DIR}/domainmodel/lsp/main.cpp)
add_library(domainmodelLsp ${_lsp})
target_link_libraries(domainmodelLsp PUBLIC domainmodelCore cursiv::lsp)
add_executable(domainmodel_lsp ${CMAKE_CURRENT_SOURCE_DIR}/domainmodel/lsp/main.cpp)
target_link_libraries(domainmodel_lsp PRIVATE domainmodelLsp)

cursiv_package_language(domainmodel)
