cmake_minimum_required(VERSION 3.30)

include("${PARENT}/refusals/settings.cmake")
project(cursiv_refusal LANGUAGES CXX)
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

find_package(Threads REQUIRED)
include("${PARENT}/refusals/engines.cmake")
add_library(cursiv::core ALIAS cursivCore)
add_library(cursiv::lr_gen ALIAS cursivLrGen)
add_library(cursiv::ll_gen ALIAS cursivLlGen)
add_compile_options(${CURSIV_REFUSAL_COMPILE_OPTIONS})
add_compile_definitions(${CURSIV_REFUSAL_COMPILE_DEFINITIONS})

include("${CURSIV_REFUSAL_ROOT}/cmake/CursivGrammar.cmake")
cursiv_grammar_generator(refusal_${LANE} "refusals/${CASE}.hpp" refuse::G
    refuse::generated::G "" "" "" "${CURSIV_REFUSAL_ROOT}/tests" ${ENGINE})
target_compile_options(refusal_${LANE}_gen PRIVATE
    # Wide invalid grammars need diagnostics without retaining debug types.
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CXX_COMPILER_FRONTEND_VARIANT:GNU>>:-g0>
    $<$<CXX_COMPILER_ID:Clang>:-ferror-limit=0>)
add_custom_target(refuse
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    COMMAND $<TARGET_FILE:refusal_${LANE}_gen>
            "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    DEPENDS refusal_${LANE}_gen
    VERBATIM)
