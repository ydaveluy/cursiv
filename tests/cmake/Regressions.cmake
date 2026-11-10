cursiv_test(TapeAudit_test engines/lr/TapeAudit_test.cpp
    tortureGenerated
    cellsGenerated
    edgesGenerated)
target_compile_definitions(TapeAudit_test PRIVATE
    CURSIV_TAPE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/torture/corpus")
# Guards, refused word heads and shifting candidates use the same lexical policy.
cursiv_grammar(guard engines/lr/LexGuardGrammars.hpp tguard::G ENGINE LR)
cursiv_grammar(guardtrivia engines/lr/LexGuardGrammars.hpp tguard::trivia::G ENGINE LR)
# Every mode of `Reduced` elects one candidate: a table without candidates.
cursiv_grammar(samewordreduced engines/lr/SameWordCandidatesGrammar.hpp
    tsameword::Reduced ENGINE LR)
# The ceilings a parse reaches, counted: look-ahead, graph and repair budgets.
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
# Random grammars checked against an Earley parse. Host only: the build
# generates a parser for each seed.
if(NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(oracle)
endif()

# One grammar for each graph-growth class: no fork, linear, quadratic and
# cubic, with the expected AST of the elected tree.
cursiv_grammar(growthfastalr engines/lr/GrowthGrammars.hpp tgfasta::Lr ENGINE LR)
cursiv_grammar(growth engines/lr/GrowthGrammars.hpp tgrowth::G ENGINE LR)
cursiv_test(LrGrowth_test engines/lr/LrGrowth_test.cpp
    cursiv::bench growthfastalrGenerated growthGenerated)
# A project may build strict C++: a table with no competitors must not size an
# array at zero.
target_compile_options(LrGrowth_test PRIVATE -pedantic-errors)
set_tests_properties(LrGrowth_test PROPERTIES TIMEOUT 120)
