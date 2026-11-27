cursiv_grammar(lexicalbackendslr engines/shared/LexicalBackendsGrammar.hpp tlexbackends::Suite)

cursiv_grammar(characterslr engines/shared/CharactersGrammar.hpp tchars::Lr)

# Trivia: ignored terminals that tie, trivia that is no bracket, the text an
# implied token's condition reads, and a newline a block entry does not read.

cursiv_grammar(trivialr engines/shared/TriviaGrammar.hpp ttrivia::Lr)

cursiv_grammar(regionboundarylr engines/shared/RegionBoundaryGrammar.hpp tregionboundary::Suite)
cursiv_test(RegionBoundary_test engines/shared/RegionBoundary_test.cpp
    regionboundarylrGenerated)

cursiv_test(RegionConsumption_test engines/shared/RegionConsumption_test.cpp
    regionboundarylrGenerated)

# An operator written in place as a sequence reads as an unnamed data rule.
cursiv_grammar(infixoperatorslr engines/shared/InfixOperatorsGrammar.hpp toperators::Suite)
# Prefix and postfix levels in a precedence table, and where a line break ends
# an operand.
cursiv_grammar(unarylevelslr engines/shared/UnaryLevelsGrammar.hpp tunary::Suite)

# A class's members are visited in declaration order, whatever order the
# grammar names them in.
cursiv_grammar(backwardlr engines/shared/AstTreeGrammars.hpp tmember::Backward)

cursiv_grammar(eofregionlr engines/shared/EofRegionGrammar.hpp teofregion::Suite)
cursiv_grammar(eofregionaux engines/shared/EofRegionGrammar.hpp teofregion::Aux)

cursiv_grammar(eofpurelr engines/shared/EofRegionGrammar.hpp teofregion::PureBase)

cursiv_grammar(guardedclimblr engines/shared/GuardedClimbGrammar.hpp tguardedclimb::Base)

cursiv_grammar(sectionpredicatelr engines/shared/SectionPredicateGrammar.hpp tsecpred::Suite)
cursiv_grammar(hushedlr engines/shared/SectionPredicateGrammar.hpp tsecpred::Hushed)

# Documented, plain and inline alternatives that give the same values.
cursiv_grammar(branchvalueslr engines/shared/BranchValuesGrammar.hpp tbranch::Suite)

cursiv_grammar(asttreelr engines/shared/AstTreeGrammars.hpp ttree::Combined)
cursiv_grammar(asttreeaux engines/shared/AstTreeGrammars.hpp ttree::Aux)

# The conformance cases of the grammar reference.
cursiv_grammar(conformancelr engines/shared/ConformanceGrammars.hpp tconformance::Suite)

cursiv_test(RecursiveTerminals_test engines/shared/RecursiveTerminals_test.cpp
    lexicalbackendslrGenerated)
cursiv_test(LexicalBackends_test engines/shared/LexicalBackends_test.cpp
    lexicalbackendslrGenerated)
cursiv_test(Characters_test engines/shared/Characters_test.cpp
    characterslrGenerated)
cursiv_test(Trivia_test engines/shared/Trivia_test.cpp
    trivialrGenerated)
cursiv_test(InfixOperators_test engines/shared/InfixOperators_test.cpp
    infixoperatorslrGenerated unarylevelslrGenerated)
cursiv_test(EofRegion_test engines/shared/EofRegion_test.cpp
    eofregionlrGenerated eofregionauxGenerated eofpurelrGenerated)
cursiv_test(GuardedClimb_test engines/shared/GuardedClimb_test.cpp
    guardedclimblrGenerated)
# The cells of several actions a generator names on demand, each after the
# run of symbols that leads to it; nothing shifts into the start state.
if(NOT CMAKE_CROSSCOMPILING)
  add_test(NAME conflict_report COMMAND ${CMAKE_COMMAND}
      -DGUARDED=$<TARGET_FILE:guardedclimblr_gen>
      -DFACTORING=$<TARGET_FILE:conformancelr_gen>
      -DCONDITIONS=$<TARGET_FILE:unarylevelslr_gen>
      -DFLAGS=$<TARGET_FILE:asttreelr_gen>
      -DINDENT=$<TARGET_FILE:indent_gen>
      -P ${CMAKE_CURRENT_LIST_DIR}/grammar_diagnostics.cmake)
endif()
cursiv_test(AstTree_test
    "engines/shared/AstTree_test.cpp;engines/shared/AstMaterialise_cases.cpp;engines/shared/DirectiveSequence_cases.cpp"
    asttreelrGenerated asttreeauxGenerated backwardlrGenerated)
cursiv_test(Conformance_test
    "engines/shared/Conformance_test.cpp;engines/shared/ConditionContract_cases.cpp;engines/shared/SectionPredicate_cases.cpp;engines/shared/BranchValues_cases.cpp"
    conformancelrGenerated sectionpredicatelrGenerated hushedlrGenerated branchvalueslrGenerated)
set_target_properties(AstTree_test Conformance_test PROPERTIES
    UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)
cursiv_timeout(Conformance_test 30)
cursiv_timeout(InfixOperators_test 30)
cursiv_timeout(LexicalBackends_test 30)
cursiv_timeout(RecursiveTerminals_test 30)

# Guards that decide where a token ends: a keyword under a
# complemented word boundary, and a passable refusal past a terminal's head.
cursiv_grammar(lexguardslr engines/shared/LexGuardsGrammar.hpp tlexguards::Main)
# An assertion that decides whether a longer token reads over a shorter one.
cursiv_grammar(lexovertakelr engines/shared/LexGuardsGrammar.hpp
    tlexguards::Overtake)
# A token that starts with a blank behind an assertion.
cursiv_grammar(lexblankheadlr engines/shared/LexGuardsGrammar.hpp
    tlexguards::BlankHead)
cursiv_test(LexGuards_test engines/shared/LexGuards_test.cpp
    lexguardslrGenerated lexovertakelrGenerated lexblankheadlrGenerated)
