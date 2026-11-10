# Languages refused at configure, one per `CASE`: configured by
# `refused_grammar_name_test`, `refused_file_name_test` and
# `refused_missing_value_test`, which read the refusal.
cmake_minimum_required(VERSION 3.30)
project(cursiv_refused_grammar_name NONE)

include(${CURSIV_SOURCE_DIR}/cmake/CursivGrammar.cmake)

if(CASE STREQUAL "missing-value")
  # An engine left out after its keyword, which the default would hide.
  cursiv_generate_language(ID        refused
      GRAMMAR    refused.hpp
      NAMESPACE  refused::Refused
      OUT        src/refused/generated
      ENGINE)
elseif(CASE STREQUAL "file-name")
  # A file name the generator would read as two.
  cursiv_generate_language(ID        refused
      GRAMMAR    refused.hpp
      NAMESPACE  refused::Refused
      FILE_NAMES "my,file"
      ENGINE     LR
      OUT        src/refused/generated)
else()
  # A grammar header named like a source of its generator, which includes it
  # quoted.
  cursiv_generate_language(ID        refused
      GRAMMAR   lr_build.hpp
      NAMESPACE refused::Refused
      ENGINE    LR
      OUT       src/refused/generated)
endif()
