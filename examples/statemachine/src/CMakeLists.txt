cursiv_generate_language(ID         statemachine
    GRAMMAR    statemachine/core/StateMachineGrammar.hpp
    NAMESPACE  sm::SM
    EXTENSIONS .statemachine
    OUT        statemachine/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/statemachine/core/*.cpp)
add_library(statemachineCore ${_core})
target_link_libraries(statemachineCore PUBLIC statemachineGenerated)

file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/statemachine/cli/*.cpp)
add_executable(statemachine_cli ${_cli})
target_link_libraries(statemachine_cli PRIVATE statemachineCore cursiv::cli)

file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/statemachine/lsp/*.cpp)
list(REMOVE_ITEM _lsp ${CMAKE_CURRENT_SOURCE_DIR}/statemachine/lsp/main.cpp)
add_library(statemachineLsp ${_lsp})
target_link_libraries(statemachineLsp PUBLIC statemachineCore cursiv::lsp)
add_executable(statemachine_lsp statemachine/lsp/main.cpp)
target_link_libraries(statemachine_lsp PRIVATE statemachineLsp)

cursiv_package_language(statemachine)
