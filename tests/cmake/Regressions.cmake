cursiv_test(TapeAudit_test engines/lr/TapeAudit_test.cpp
    tortureGenerated
    cellsGenerated
    unsettledGenerated)
target_compile_definitions(TapeAudit_test PRIVATE
    CURSIV_TAPE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/torture/corpus")
cursiv_grammar(guard engines/lr/LexGuardGrammars.hpp tguard::G ENGINE LR)
cursiv_grammar(guardtrivia engines/lr/LexGuardGrammars.hpp tguard::trivia::G ENGINE LR)
foreach(_n IN ITEMS Reduced Shifted)
  string(TOLOWER ${_n} _id)
  cursiv_grammar(alike${_id} engines/lr/AlikeCandidatesGrammar.hpp talike::${_n} ENGINE LR)
endforeach()
# The ceilings a parse meets, counted: look-ahead, graph and repair budgets.
cursiv_grammar(ceilings engines/lr/CeilingsGrammar.hpp tceilings::G ENGINE LR)
cursiv_test(LrCeilings_test engines/lr/LrCeilings_test.cpp
    cellsGenerated
    danglingGenerated
    room221Generated
    ambtailGenerated
    ceilingsGenerated)
set_tests_properties(LrCeilings_test PROPERTIES TIMEOUT 120)
cursiv_grammar(runonlr engines/lr/RunOnEquationsGrammar.hpp trunon::Lr ENGINE LR)
cursiv_test(RunOnEquations_test engines/lr/RunOnEquations_test.cpp
    runonlrGenerated)
set_tests_properties(RunOnEquations_test PROPERTIES TIMEOUT 120)
cursiv_grammar(keywordbehindlr engines/lr/KeywordHandoffGrammar.hpp
    tkeyword::BehindLr ENGINE LR)
cursiv_grammar(room221 engines/lr/FrozenRoomGrammars.hpp troom::seed221::G ENGINE LR)
cursiv_grammar(repaircost engines/lr/RepairCostGrammar.hpp trepair::G ENGINE LR)
# Repairs over long documents: their cost, their ledger, and the arms a repaired
# reading forks.
# Random grammars against an Earley reading of each: every seed is a grammar
# the build generates, so the lane runs where the generator does.
if(NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(oracle)
endif()

# One representative for each graph-growth class: no fork, linear,
# quadratic and cubic, with fixed AST oracles for the elected tree.
cursiv_grammar(growthfastalr engines/lr/GrowthGrammars.hpp tgfasta::Lr ENGINE LR)
cursiv_grammar(growth engines/lr/GrowthGrammars.hpp tgrowth::G ENGINE LR)
cursiv_test(LrGrowth_test engines/lr/LrGrowth_test.cpp
    growthfastalrGenerated growthGenerated)
set_tests_properties(LrGrowth_test PROPERTIES TIMEOUT 120)
