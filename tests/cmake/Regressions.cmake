cursiv_test(TapeAudit_test engines/lr/TapeAudit_test.cpp
    tortureGenerated
    cellsGenerated
    edgesGenerated)
target_compile_definitions(TapeAudit_test PRIVATE
    CURSIV_TAPE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/torture/corpus")
# Guards, refused word heads and shifting candidates use the same lexical policy.
cursiv_grammar(guard engines/lr/LexGuardGrammars.hpp tguard::G ENGINE LR)
cursiv_grammar(guardtrivia engines/lr/LexGuardGrammars.hpp tguard::trivia::G ENGINE LR)
# Unlike the shared guard table, every mode of Reduced elects one candidate.
# Keep its candidate-free generated parser as an independent contract.
cursiv_grammar(alikereduced engines/lr/AlikeCandidatesGrammar.hpp
    talike::Reduced ENGINE LR)
# The ceilings a parse meets, counted: look-ahead, graph and repair budgets.
cursiv_grammar(ceilings engines/lr/CeilingsGrammar.hpp tceilings::G ENGINE LR)
cursiv_test(LrCeilings_test engines/lr/LrCeilings_test.cpp
    cellsGenerated
    electionGenerated
    room221Generated
    ambtailGenerated
    ceilingsGenerated)
cursiv_timeout(LrCeilings_test 120)
cursiv_grammar(runonlr engines/lr/RunOnEquationsGrammar.hpp trunon::Lr ENGINE LR)
cursiv_test(RunOnEquations_test engines/lr/RunOnEquations_test.cpp
    runonlrGenerated)
set_tests_properties(RunOnEquations_test PROPERTIES TIMEOUT 120)
cursiv_grammar(room221 engines/lr/FrozenRoomGrammars.hpp troom::seed221::G ENGINE LR)
cursiv_grammar(repaircost engines/lr/RepairCostGrammar.hpp trepair::G ENGINE LR)
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
