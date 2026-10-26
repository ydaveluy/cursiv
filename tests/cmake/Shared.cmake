cursiv_grammar_pair(lexicalbackends engines/shared/LexicalBackendsGrammar.hpp tlexbackends::Lr tlexbackends::Ll)

cursiv_grammar(characterslr engines/shared/CharactersGrammar.hpp tchars::Lr ENGINE LR)

# Trivia: two ignored terminals reading one text are no tie, and a terminal
# read as trivia is no bracket.
# The text an implied token's condition reads lexes whole where the token may
# be implied, in both engines.

cursiv_grammar_pair(trivia engines/shared/TriviaGrammar.hpp ttrivia::Lr ttrivia::Ll)

# An operator written in place as a sequence reads as an unnamed data rule, in
# both engines.
cursiv_grammar_pair(infixoperators engines/shared/InfixOperatorsGrammar.hpp toperators::Lr toperators::Ll)

# A class's members are visited in its declaration order, whichever order the
# arms of the grammar name them in, in both engines.
# The reversed first encounter exercises the shared metadata emitter once.
cursiv_grammar(backwardlr engines/shared/AstTreeGrammars.hpp tmember::BackwardLr ENGINE LR)

# Every grammar the build ships, and the lane's own: those carry the guards,
# the reserves and the wide tables no shipped grammar reaches.
cursiv_grammar(onemode engines/lr/OneModeGrammar.hpp tonemode::G ENGINE LR)
cursiv_grammar(candidateruns engines/lr/CandidateRunsGrammar.hpp tcandrun::G ENGINE LR)
# The lexer's tables: candidate runs, typo alternatives, guards and conversions,
# wide terminal sets and converters that read memory.
cursiv_test(LrLexicon_test engines/lr/LrLexicon_test.cpp alikereducedGenerated
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

cursiv_grammar_pair(asttree engines/shared/AstTreeGrammars.hpp ttree::Lr ttree::Ll)
cursiv_aux_grammar(asttreeaux engines/shared/AstTreeGrammars.hpp ttree::Aux)

# The conformance cases of the grammar reference, on both engines.
cursiv_grammar_pair(conformance engines/shared/ConformanceGrammars.hpp tconformance::Lr tconformance::Ll)

# Shared suites run after their grammar families have been declared.
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
    infixoperatorsllGenerated infixoperatorslrGenerated)
cursiv_test(EofRegion_test engines/shared/EofRegion_test.cpp
    eofregionllGenerated eofregionlrGenerated eofregionauxGenerated
    eofpurellGenerated eofpurelrGenerated)
cursiv_test(GuardedClimb_test engines/shared/GuardedClimb_test.cpp
    guardedclimbllGenerated guardedclimblrGenerated)
cursiv_test(LexSuffixSession_test engines/shared/LexSuffixSession_test.cpp)
cursiv_test(AstTree_test
    "engines/shared/AstTree_test.cpp;engines/shared/AstMaterialise_cases.cpp;engines/shared/DirectiveSequence_cases.cpp"
    cursivLlGen cursivLrGen asttreellGenerated asttreelrGenerated
    asttreeauxGenerated backwardlrGenerated)
set_target_properties(AstTree_test PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)
cursiv_test(Conformance_test
    "engines/shared/Conformance_test.cpp;engines/shared/ConditionContract_cases.cpp;engines/shared/SectionPredicate_cases.cpp"
    conformancellGenerated conformancelrGenerated
    sectionpredicatellGenerated sectionpredicatelrGenerated)
set_target_properties(Conformance_test PROPERTIES
    UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)
set_tests_properties(LexicalBackends_test PROPERTIES TIMEOUT 30)
set_tests_properties(RecursiveTerminals_test PROPERTIES TIMEOUT 30)
set_tests_properties(TypoCandidates_test PROPERTIES TIMEOUT 30)
