cursiv_generate_language(ID         arithmetics
    GRAMMAR    arithmetics/core/ArithmeticsGrammar.hpp
    NAMESPACE  arith::Arith
    EXTENSIONS .calc
    OUT        arithmetics/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/arithmetics/core/*.cpp)
add_library(arithmeticsCore ${_core})
target_link_libraries(arithmeticsCore PUBLIC arithmeticsGenerated)

file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/arithmetics/cli/*.cpp)
add_executable(arithmetics_cli ${_cli})
target_link_libraries(arithmetics_cli PRIVATE arithmeticsCore cursiv::cli)

file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/arithmetics/lsp/*.cpp)
list(REMOVE_ITEM _lsp ${CMAKE_CURRENT_SOURCE_DIR}/arithmetics/lsp/main.cpp)
add_library(arithmeticsLsp ${_lsp})
target_link_libraries(arithmeticsLsp PUBLIC arithmeticsCore cursiv::lsp)
add_executable(arithmetics_lsp arithmetics/lsp/main.cpp)
target_link_libraries(arithmetics_lsp PRIVATE arithmeticsLsp)

cursiv_package_language(arithmetics)
