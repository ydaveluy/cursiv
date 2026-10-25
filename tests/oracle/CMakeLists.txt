# Random grammars against an Earley reading of the same grammar: a seed is a
# grammar `rgram_write` draws, the build generates and a lane of its own reads
# document by document. The oracle reads the grammar alone, never a table.

add_library(rgramOracle STATIC RgramGrammar.cpp RgramCfg.cpp rgram_lane.cpp)
target_include_directories(rgramOracle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(rgramOracle PUBLIC cursiv::core)

add_executable(rgram_write rgram_write.cpp)
target_link_libraries(rgram_write PRIVATE rgramOracle)

set(CURSIV_RGRAM_SEED 1 CACHE STRING
    "The first seed of the long random-grammar run")
set(CURSIV_RGRAM_COUNT 0 CACHE STRING
    "How many grammars the long random-grammar run draws; zero, none")

# One seed: its header, its generator, the run that tolerates a refusal, and
# the lane compiled against what that run left.
function(cursiv_rgram seed)
  set(id rgram${seed})
  set(root ${CMAKE_CURRENT_BINARY_DIR}/grammars)
  set(own ${root}/${id})
  add_custom_command(OUTPUT ${own}/Grammar.hpp
      COMMAND ${CMAKE_COMMAND} -E make_directory ${own}
      COMMAND rgram_write ${seed} ${own}/Grammar.hpp
      DEPENDS rgram_write
      COMMENT "${id}: drawing its grammar")
  add_custom_target(${id}_grammar DEPENDS ${own}/Grammar.hpp)
  cursiv_grammar_generator(${id} "${id}/Grammar.hpp" ${id}::G
                            ${id}::generated::G "" "" "" "${root}" LR)
  add_dependencies(${id}_gen ${id}_grammar)
  add_custom_command(OUTPUT ${own}/generated.stamp
      COMMAND ${CMAKE_COMMAND} -DGEN=$<TARGET_FILE:${id}_gen>
              -DDIR=${own}/generated/G -DREFUSED=${own}/refused.txt
              -DSTAMP=${own}/generated.stamp
              -P ${CMAKE_CURRENT_SOURCE_DIR}/rgram_generate.cmake
      DEPENDS ${id}_gen ${CMAKE_CURRENT_SOURCE_DIR}/rgram_generate.cmake
      COMMENT "${id}: generating its parser")
  # A unit compiles to nothing where the grammar was refused, so each one
  # recompiles after every run of the generator.
  file(CONFIGURE OUTPUT ${own}/check.cpp
       CONTENT "#include \"${CMAKE_CURRENT_SOURCE_DIR}/rgram_check.cpp\"\n")
  set(units ${own}/check.cpp)
  foreach(unit ParserStates AstReflection Grammar Module)
    set(inc "${id}/generated/G/${unit}.cpp")
    file(CONFIGURE OUTPUT ${own}/${unit}.cpp CONTENT
         "#if __has_include(\"${inc}\")\n#include \"${inc}\"\n#endif\n")
    list(APPEND units ${own}/${unit}.cpp)
  endforeach()
  set_source_files_properties(${units} PROPERTIES
      OBJECT_DEPENDS ${own}/generated.stamp)
  add_executable(${id}_check ${units})
  target_include_directories(${id}_check PRIVATE ${root})
  target_compile_definitions(${id}_check PRIVATE
      RGRAM_SEED=${seed} RGRAM_NS=${id}
      RGRAM_MODULE="${id}/generated/G/Module.hpp"
      RGRAM_REFUSED="${own}/refused.txt")
  target_link_libraries(${id}_check PRIVATE rgramOracle)
endfunction()

# One accepted seed per family. 107 and 242 close a region on a rule;
# in 107, 169 and 189 a list's continuation shifts a token the state owes.
set(_rgram_fixed 107 169 189 242 296 164 126 223)
foreach(seed IN LISTS _rgram_fixed)
  cursiv_rgram(${seed})
  add_test(NAME rgram_${seed} COMMAND rgram${seed}_check)
  set_tests_properties(rgram_${seed} PROPERTIES LABELS rgram TIMEOUT 120)
endforeach()

# The long run, asked for at configure (`-DCURSIV_RGRAM_COUNT=<n>
# -DCURSIV_RGRAM_SEED=<first>`), built by `rgram-long` and run by
# `ctest -L rgram-long`: there a grammar the generator refuses passes.
if(CURSIV_RGRAM_COUNT GREATER 0)
  add_custom_target(rgram-long)
  math(EXPR _last "${CURSIV_RGRAM_SEED} + ${CURSIV_RGRAM_COUNT} - 1")
  foreach(seed RANGE ${CURSIV_RGRAM_SEED} ${_last})
    if(NOT TARGET rgram${seed}_check)
      cursiv_rgram(${seed})
      set_target_properties(rgram${seed}_check PROPERTIES EXCLUDE_FROM_ALL ON)
    endif()
    add_dependencies(rgram-long rgram${seed}_check)
    add_test(NAME rgram_long_${seed} COMMAND rgram${seed}_check --may-refuse)
    set_tests_properties(rgram_long_${seed} PROPERTIES
        LABELS rgram-long TIMEOUT 120)
  endforeach()
endif()
