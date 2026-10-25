# A grammar header named like a source of its generator, which includes it
# quoted: configured by `refused_grammar_name_test`, which reads the refusal.
cmake_minimum_required(VERSION 3.30)
project(cursiv_refused_grammar_name NONE)

include(${CURSIV_SOURCE_DIR}/cmake/CursivGrammar.cmake)

cursiv_generate_language(ID        refused
    GRAMMAR   lr_build.hpp
    NAMESPACE refused::Refused
    ENGINE    LR
    OUT       src/refused/generated)
