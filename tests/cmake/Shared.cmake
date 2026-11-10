cursiv_grammar_pair(lexicalbackends engines/shared/LexicalBackendsGrammar.hpp tlexbackends::Lr tlexbackends::Ll)

cursiv_grammar(characterslr engines/shared/CharactersGrammar.hpp tchars::Lr ENGINE LR)

# Trivia: ignored terminals that tie, trivia that is no bracket, the text an
# implied token's condition reads, and a newline a block entry does not read.

cursiv_grammar_pair(trivia engines/shared/TriviaGrammar.hpp ttrivia::Lr ttrivia::Ll)

cursiv_grammar_pair(regionboundary engines/shared/RegionBoundaryGrammar.hpp tregionboundary::Lr tregionboundary::Ll)
cursiv_test(LexSkipSet_test engines/shared/LexSkipSet_test.cpp)
cursiv_test(RegionBoundary_test engines/shared/RegionBoundary_test.cpp
    regionboundaryllGenerated regionboundarylrGenerated)
cursiv_test(RegionBoundaryAnalysis_test engines/shared/RegionBoundaryAnalysis_test.cpp
    cursivLlGen cursivLrGen)

cursiv_test(RegionConsumption_test engines/shared/RegionConsumption_test.cpp
    regionboundaryllGenerated regionboundarylrGenerated)

# An operator written in place as a sequence reads as an unnamed data rule, in
# both engines.
cursiv_grammar_pair(infixoperators engines/shared/InfixOperatorsGrammar.hpp toperators::Lr toperators::Ll)
# Prefix and postfix levels in a precedence table, and where a line break ends
# an operand, in both engines.
cursiv_grammar_pair(unarylevels engines/shared/UnaryLevelsGrammar.hpp tunary::Lr tunary::Ll)

# A class's members are visited in declaration order, whatever order the
# grammar names them in.
cursiv_grammar(backwardlr engines/shared/AstTreeGrammars.hpp tmember::BackwardLr ENGINE LR)

cursiv_grammar(onemode engines/lr/OneModeGrammar.hpp tonemode::G ENGINE LR)
cursiv_grammar(candidateruns engines/lr/CandidateRunsGrammar.hpp tcandrun::G ENGINE LR)
# The lexer's tables: candidate runs, typo alternatives, guards and conversions,
# wide terminal sets and converters that read memory.
cursiv_test(LrLexicon_test engines/lr/LrLexicon_test.cpp samewordreducedGenerated
    backrefGenerated candidaterunsGenerated guardGenerated
    guardtriviaGenerated
    lrauditGenerated astfusedlrGenerated onemodeGenerated relayoverlapGenerated
    rulerefusalGenerated shorterlrGenerated
    widetermsGenerated cursivLrGen)
if(cursiv_IS_TOP_LEVEL AND CURSIV_DEFAULT_ENGINE STREQUAL "LR")
  target_link_libraries(LrLexicon_test PRIVATE arithmeticsGenerated)
  target_compile_definitions(LrLexicon_test PRIVATE CURSIV_LR_LEXER_CONTRACT=1
      CURSIV_EXAMPLE_DIR="${PROJECT_SOURCE_DIR}/examples")
endif()
# What a generated module builds: fused records, adopting and creating entries,
# namespaces and header names, and the lowering's symbol space.
cursiv_test(LrAst_test engines/lr/LrAst_test.cpp astfusedlrGenerated adoptGenerated
    shadowlrGenerated shadowllGenerated siblinglrGenerated siblingmoduleGenerated
    cursivLrGen)
# Grammar headers named like what the generator writes, at the top of their
# own include root: spelled bare, as their authors would.
cursiv_grammar(siblinglr Grammar.hpp tsibling::Lr ENGINE LR
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/engines/lr/sibling)
cursiv_grammar(siblingmodule Module.hpp tsibling::Module ENGINE LR
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/engines/lr/sibling)

cursiv_grammar_pair(eofregion engines/shared/EofRegionGrammar.hpp teofregion::Lr teofregion::Ll)
cursiv_aux_grammar(eofregionaux engines/shared/EofRegionGrammar.hpp teofregion::Aux)

cursiv_grammar_pair(eofpure engines/shared/EofRegionGrammar.hpp teofregion::PureLr teofregion::PureLl)

cursiv_grammar_pair(guardedclimb engines/shared/GuardedClimbGrammar.hpp tguardedclimb::Lr tguardedclimb::Ll)

cursiv_grammar_pair(sectionpredicate engines/shared/SectionPredicateGrammar.hpp tsecpred::Lr tsecpred::Ll)

# A terminal whose alternatives each give a constant, beside the data rule
# giving the same constants, in both engines.
cursiv_grammar_pair(branchvalues engines/shared/BranchValuesGrammar.hpp tbranch::Lr tbranch::Ll)

cursiv_grammar_pair(asttree engines/shared/AstTreeGrammars.hpp ttree::Lr ttree::Ll)
cursiv_aux_grammar(asttreeaux engines/shared/AstTreeGrammars.hpp ttree::Aux)

# The conformance cases of the grammar reference, on both engines.
cursiv_grammar_pair(conformance engines/shared/ConformanceGrammars.hpp tconformance::Lr tconformance::Ll)

cursiv_test(LexNfa_test engines/shared/LexNfa_test.cpp
    cursivLlGen cursivLrGen)
cursiv_test(TypoCandidates_test engines/shared/TypoCandidates_test.cpp)
cursiv_test(AstReducer_test engines/shared/AstReducer_test.cpp)
cursiv_test(NonConsuming_test engines/shared/NonConsuming_test.cpp
    cursivLlGen cursivLrGen cursivGen)
cursiv_test(AstGeneration_test engines/shared/AstGeneration_test.cpp
    cursivLlGen cursivLrGen)
cursiv_test(RecursiveTerminals_test engines/shared/RecursiveTerminals_test.cpp
    lexicalbackendsllGenerated lexicalbackendslrGenerated cursivLlGen cursivLrGen)
cursiv_test(LexicalBackends_test engines/shared/LexicalBackends_test.cpp
    lexicalbackendslrGenerated lexicalbackendsllGenerated)
cursiv_test(Characters_test engines/shared/Characters_test.cpp
    characterslrGenerated lexicalbackendsllGenerated)
cursiv_test(Trivia_test engines/shared/Trivia_test.cpp
    triviallGenerated trivialrGenerated)
cursiv_test(InfixOperators_test engines/shared/InfixOperators_test.cpp
    infixoperatorsllGenerated infixoperatorslrGenerated unarylevelsllGenerated
    unarylevelslrGenerated)
cursiv_test(EofRegion_test engines/shared/EofRegion_test.cpp
    eofregionllGenerated eofregionlrGenerated eofregionauxGenerated
    eofpurellGenerated eofpurelrGenerated)
cursiv_test(GuardedClimb_test engines/shared/GuardedClimb_test.cpp
    guardedclimbllGenerated guardedclimblrGenerated)
# The conflicts a generator names on demand: a guarded shift goes to the states
# its outcomes reach, and nothing shifts into the start state.
if(NOT CMAKE_CROSSCOMPILING)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/conflict_report)
  add_test(NAME conflict_report COMMAND guardedclimblr_gen
      ${CMAKE_CURRENT_BINARY_DIR}/conflict_report)
  set_tests_properties(conflict_report PROPERTIES
      ENVIRONMENT CURSIV_CONFLICTS=1
      PASS_REGULAR_EXPRESSION "shift   -> state [1-9]"
      FAIL_REGULAR_EXPRESSION "shift   -> state 0\n")
endif()
cursiv_test(LexSuffixSession_test engines/shared/LexSuffixSession_test.cpp)
cursiv_test(AstTree_test
    "engines/shared/AstTree_test.cpp;engines/shared/AstMaterialise_cases.cpp;engines/shared/DirectiveSequence_cases.cpp"
    cursivLlGen cursivLrGen asttreellGenerated asttreelrGenerated
    asttreeauxGenerated backwardlrGenerated)
cursiv_test(Conformance_test
    "engines/shared/Conformance_test.cpp;engines/shared/ConditionContract_cases.cpp;engines/shared/SectionPredicate_cases.cpp;engines/shared/BranchValues_cases.cpp"
    conformancellGenerated conformancelrGenerated
    sectionpredicatellGenerated sectionpredicatelrGenerated
    branchvaluesllGenerated branchvalueslrGenerated)
set_target_properties(AstTree_test Conformance_test PROPERTIES
    UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)
set_tests_properties(LexicalBackends_test RecursiveTerminals_test
    TypoCandidates_test PROPERTIES TIMEOUT 30)

# Conditions tested at each token of a run cost the run's length, on both
# engines.
cursiv_grammar_pair(condgrowth engines/shared/ConditionGrowthGrammar.hpp tcondgrowth::Lr tcondgrowth::Ll)
cursiv_test(ConditionGrowth_test engines/shared/ConditionGrowth_test.cpp
    cursiv::bench condgrowthllGenerated condgrowthlrGenerated)

# What a generator's analyses cost as a grammar built at run time grows.
cursiv_test(GeneratorGrowth_test engines/shared/GeneratorGrowth_test.cpp
    cursivLlGen cursivLrGen)

# Guards that decide where a token ends, in both engines: a keyword under a
# complemented word boundary, and a passable refusal past a terminal's head.
cursiv_grammar_pair(lexguards engines/shared/LexGuardsGrammar.hpp tlexguards::Lr tlexguards::Ll)
# An assertion that decides whether a longer token reads over a shorter one.
cursiv_grammar_pair(lexovertake engines/shared/LexGuardsGrammar.hpp
    tlexguards::OvertakeLr tlexguards::OvertakeLl)
# A token that starts with a blank behind an assertion.
cursiv_grammar_pair(lexblankhead engines/shared/LexGuardsGrammar.hpp
    tlexguards::BlankHeadLr tlexguards::BlankHeadLl)
cursiv_test(LexGuards_test engines/shared/LexGuards_test.cpp
    lexguardsllGenerated lexguardslrGenerated
    lexovertakellGenerated lexovertakelrGenerated
    lexblankheadllGenerated lexblankheadlrGenerated)
cursiv_test(PredicateState_test engines/shared/PredicateState_test.cpp)
