cursiv_grammar(semanticll engines/ll/SemanticGrammar.hpp tllsemantic::Ll ENGINE LL)
# Representative LL languages are shared by semantic, prediction and repair suites.
cursiv_grammar(boundasciilr engines/ll/ReservedBoundGrammar.hpp tllbound::AsciiLr ENGINE LR)
cursiv_grammar(boundasidelr engines/ll/ReservedBoundGrammar.hpp tllbound::AsideLr ENGINE LR)
cursiv_grammar(boundfrontierlr engines/ll/ReservedBoundGrammar.hpp tllbound::FrontierLr ENGINE LR)
cursiv_grammar_pair(boundwide engines/ll/ReservedBoundGrammar.hpp tllbound::WideLr tllbound::WideLl)
cursiv_grammar_pair(compound engines/ll/CompoundOpGrammar.hpp tllcompound::Lr tllcompound::Ll)
cursiv_grammar_pair(conditions engines/ll/ConditionsGrammar.hpp tllconditions::Lr tllconditions::Ll)
cursiv_grammar_pair(counted engines/ll/CountedRepeatGrammar.hpp tllcounted::Lr tllcounted::Ll)
cursiv_grammar(dedentrunflatll engines/ll/DedentRunGrammar.hpp tlldedentrun::FlatLl ENGINE LL)
cursiv_grammar(dedentrunll engines/ll/DedentRunGrammar.hpp tlldedentrun::Ll ENGINE LL)
cursiv_grammar(delimll engines/ll/DelimSpellingGrammar.hpp tdelim::Ll ENGINE LL)
cursiv_grammar(escapell engines/ll/ReservedEscapeGrammar.hpp tllescape::Ll ENGINE LL)
cursiv_grammar(gluedll engines/ll/ConditionsGrammar.hpp tllconditions::GluedLl ENGINE LL)
cursiv_grammar_pair(impliedrival engines/ll/ConditionsGrammar.hpp tllconditions::RivalLr tllconditions::RivalLl)
cursiv_grammar(indirectfoldll engines/ll/ExpressionContractsGrammar.hpp tllexpressions::Ll ENGINE LL)
cursiv_grammar(indirectloopcycle engines/ll/IndirectLoopGrammar.hpp tindirectloop::CycleLr ENGINE LR)
cursiv_grammar(keywordtriviabarell engines/ll/KeywordTriviaGrammar.hpp tllkwtrivia::BareLl ENGINE LL)
cursiv_grammar(keywordtriviabasicll engines/ll/KeywordTriviaGrammar.hpp tllkwtrivia::BasicLl ENGINE LL)
cursiv_grammar(keywordtriviacapturedll engines/ll/KeywordTriviaGrammar.hpp tllkwtrivia::VariantsLl ENGINE LL)
cursiv_grammar(keywordtriviawidell engines/ll/KeywordTriviaGrammar.hpp tllkwtrivia::WideLl ENGINE LL)
cursiv_grammar_pair(leftoption engines/ll/LeftOptionGrammar.hpp tlloption::Lr tlloption::Ll)
cursiv_grammar(latewidthll engines/ll/LateWidthGrammar.hpp tlllatewidth::Ll ENGINE LL GENERATOR_ARGS --no-staged)
cursiv_grammar(lexicalpredicatesll engines/ll/LexicalPredicatesGrammar.hpp tlexicalpredicates::Ll ENGINE LL)
cursiv_grammar(lextiell engines/ll/LexTieGrammar.hpp tlllextie::Ll ENGINE LL)
cursiv_grammar(lextiereservedll engines/ll/LexTieGrammar.hpp tlllextie::ReservedLl ENGINE LL)
cursiv_grammar(llindent engines/ll/IndentParityGrammars.hpp tllindent::Ll ENGINE LL)
cursiv_grammar(llindentbare engines/ll/IndentParityGrammars.hpp tllindent::Bare ENGINE LL)
cursiv_grammar(llindentfolded engines/ll/IndentParityGrammars.hpp tllindent::Folded ENGINE LL)
cursiv_grammar(llindentlaid engines/ll/IndentParityGrammars.hpp tllindent::Laid ENGINE LL)
cursiv_grammar(openingll engines/ll/OpeningTokenGrammar.hpp topening::Ll ENGINE LL)
cursiv_grammar(openingshiftlr engines/ll/OpeningTokenGrammar.hpp topening::ShiftLr ENGINE LR)
cursiv_grammar(predicatesll engines/ll/PredicatesGrammar.hpp tpredicates::Ll ENGINE LL)
cursiv_grammar(rcondll engines/ll/GuardCasesGrammar.hpp tllguardcases::Ll ENGINE LL)
cursiv_grammar(rcondnest engines/ll/RuleConditionGrammar.hpp tllrcond::Nested ENGINE LR)
cursiv_grammar(reachpredictll engines/ll/ConditionsGrammar.hpp tllconditions::ReachLl ENGINE LL)
cursiv_grammar_pair(refused engines/ll/ReservedRefusedGrammar.hpp tllrefused::Lr tllrefused::Ll)
cursiv_grammar_pair(refusedmemo engines/ll/ReservedRefusedGrammar.hpp
    tllrefused::MemoLr tllrefused::MemoLl)
cursiv_grammar(refusedcondll engines/ll/ReservedRefusedGrammar.hpp tllrefused::CondLl ENGINE LL)
cursiv_grammar(repairgrowthll engines/ll/LlRepairGrowthGrammar.hpp tllrepairgrowth::Ll ENGINE LL)
cursiv_grammar(rescannarrowll engines/ll/RescanBoundGrammar.hpp tllrescan::NarrowLl ENGINE LL)
cursiv_grammar(rescanshadowll engines/ll/RescanBoundGrammar.hpp tllrescan::ShadowLl ENGINE LL)
cursiv_grammar(skipwidenll engines/ll/SkipWidenGrammar.hpp tllwiden::Ll ENGINE LL)
cursiv_grammar(swiftwidthll engines/ll/SwiftWidthGrammar.hpp tllswiftwidth::Ll ENGINE LL GENERATOR_ARGS --no-staged)
cursiv_grammar(termcondlr engines/ll/RuleConditionGrammar.hpp tllterm::Lr ENGINE LR)
cursiv_grammar(triviapredictll engines/ll/ConditionsGrammar.hpp tllconditions::TriviaLl ENGINE LL)
cursiv_grammar(unitsll engines/ll/ExplicitUnitGrammar.hpp tllunit::Ll ENGINE LL)
cursiv_grammar_pair(unordered engines/ll/UnorderedGrammar.hpp tunordered::Lr tunordered::Ll)
cursiv_grammar_pair(unorderedopt engines/ll/UnorderedOptionalGrammar.hpp toptional::Lr toptional::Ll)
cursiv_grammar_pair(unorderedturns engines/ll/UnorderedTurnsGrammar.hpp tunorderedturns::Lr tunorderedturns::Ll)
cursiv_grammar(widechoicell engines/ll/WideChoiceGrammar.hpp twidechoice::Ll ENGINE LL)
cursiv_grammar(wideposll engines/ll/WidePositionsGrammar.hpp twidepos::Ll ENGINE LL)

cursiv_test(LlCaretParity_test engines/ll/LlCaretParity_test.cpp
    indirectfoldllGenerated conditionsllGenerated conditionslrGenerated parityllGenerated paritylrGenerated)
cursiv_test(LlCompoundOp_test engines/ll/LlCompoundOp_test.cpp
    compoundllGenerated compoundlrGenerated)
cursiv_test(LlConditions_test engines/ll/LlConditions_test.cpp
    conditionsllGenerated conditionslrGenerated impliedrivalllGenerated
    gluedllGenerated impliedrivallrGenerated reachpredictllGenerated
    triviapredictllGenerated cursivLlGen)
cursiv_test(LlCountedRepeat_test engines/ll/LlCountedRepeat_test.cpp
    countedllGenerated countedlrGenerated semanticllGenerated cursivLlGen)
cursiv_test(LlEngine_test engines/ll/LlEngine_test.cpp
    indirectfoldllGenerated menullGenerated menulrGenerated parityllGenerated paritylrGenerated tagllGenerated taglrGenerated cursivLlGen)
cursiv_test(LlGeneration_test engines/ll/LlGeneration_test.cpp
    cursivLlGen cursivLrGen)
cursiv_test(LlHistory_test engines/ll/LlHistory_test.cpp)
cursiv_test(LlLentStands_test engines/ll/LlLentStands_test.cpp)
cursiv_test(LlIndent_test engines/ll/LlIndent_test.cpp
    indentGenerated indentrepairedGenerated indentfoldedGenerated indentimpliedGenerated llindentGenerated llindentbareGenerated llindentfoldedGenerated llindentlaidGenerated
    dedentrunflatllGenerated dedentrunllGenerated delimllGenerated cursivLlGen cursivLrGen)
cursiv_test(LlIndirectLoop_test engines/ll/LlIndirectLoop_test.cpp
    indirectloopcycleGenerated semanticllGenerated cursivLlGen)
cursiv_test(LlKeywordTrivia_test engines/ll/LlKeywordTrivia_test.cpp
    keywordtriviabarellGenerated keywordtriviabasicllGenerated keywordtriviacapturedllGenerated keywordtriviawidellGenerated)
cursiv_test(LlLateWidth_test engines/ll/LlLateWidth_test.cpp
    latewidthllGenerated)
cursiv_test(LlLexTie_test engines/ll/LlLexTie_test.cpp
    unitsllGenerated lextiellGenerated lextiereservedllGenerated)
cursiv_test(LlPredicates_test engines/ll/LlPredicates_test.cpp
    lexicalpredicatesllGenerated predicatesllGenerated)
cursiv_test(LlRecoveryParity_test engines/ll/LlRecoveryParity_test.cpp
    conditionsllGenerated conditionslrGenerated parityllGenerated
    paritylrGenerated)
cursiv_test(LlRepairBudget_test engines/ll/LlRepairBudget_test.cpp
    parityllGenerated paritylrGenerated)
cursiv_test(LlRepairGrowth_test engines/ll/LlRepairGrowth_test.cpp
    parityllGenerated repairgrowthllGenerated)
cursiv_test(LlRescanBound_test engines/ll/LlRescanBound_test.cpp
    rescannarrowllGenerated rescanshadowllGenerated)
cursiv_test(LlReservedBound_test engines/ll/LlReservedBound_test.cpp
    boundasciilrGenerated boundasidelrGenerated boundfrontierlrGenerated boundwidellGenerated boundwidelrGenerated)
cursiv_test(LlSearchBound_test engines/ll/LlSearchBound_test.cpp
    llindentGenerated parityllGenerated predicatesllGenerated sectiontailllGenerated)
cursiv_test(LlSkipWiden_test engines/ll/LlSkipWiden_test.cpp
    skipwidenllGenerated)
cursiv_test(LlSwiftWidth_test engines/ll/LlSwiftWidth_test.cpp
    swiftwidthllGenerated)
cursiv_test(LlWideChoice_test engines/ll/LlWideChoice_test.cpp
    widechoicellGenerated)
cursiv_test(LlWidePositions_test engines/ll/LlWidePositions_test.cpp
    wideposllGenerated)
cursiv_test(Unordered_test engines/ll/Unordered_test.cpp
    unorderedllGenerated unorderedlrGenerated unorderedoptllGenerated unorderedoptlrGenerated
    unorderedturnsllGenerated unorderedturnslrGenerated cursivLlGen cursivLrGen)

# A shallow template budget catches per-element recursion in the wide choice.
if(TARGET widechoicell_gen)
  target_compile_options(widechoicell_gen PRIVATE -ftemplate-depth=100)
elseif(CMAKE_CROSSCOMPILING)
  cursiv_host_manifest("target_compile_options(widechoicell_gen PRIVATE -ftemplate-depth=100)")
endif()
set_tests_properties(LlRepairBudget_test PROPERTIES TIMEOUT 30)
set_tests_properties(LlSwiftWidth_test PROPERTIES TIMEOUT 20)

# These cases all exercise semanticll; share their heavy grammar/AST headers.
set(_semantic_cases
    engines/ll/LlSemantic_test.cpp
    engines/ll/LlCaretAgreement_cases.cpp
    engines/ll/LlCaretDepth_cases.cpp
    engines/ll/LlCreationBoundary_cases.cpp
    engines/ll/LlDataText_cases.cpp
    engines/ll/LlFoldOrder_cases.cpp
    engines/ll/LlKeywordRelay_cases.cpp
    engines/ll/LlReservedWord_cases.cpp
    engines/ll/LlSkipConfigs_cases.cpp
    engines/ll/LlTextOrder_cases.cpp)
cursiv_test(LlSemantic_test "${_semantic_cases}"
    semanticllGenerated escapellGenerated indirectfoldllGenerated)

# Expressions reuse one generated language and one compilation of their AST headers.
set(_expression_cases
    engines/ll/LlExpression_test.cpp
    engines/ll/LlIndirectFold_test.cpp
    engines/ll/LlLeftOption_cases.cpp
    engines/ll/LlPrefixFactor_test.cpp
    engines/ll/LlInfixLoop_test.cpp
    engines/ll/LlOperatorCondition_test.cpp
    engines/ll/LlOpeningToken_test.cpp)
cursiv_test(LlExpression_test "${_expression_cases}"
    indirectfoldllGenerated leftoptionllGenerated leftoptionlrGenerated
    openingllGenerated openingshiftlrGenerated)

set(_guard_cases
    engines/ll/LlGuards_test.cpp
    engines/ll/LlRuleCondition_test.cpp
    engines/ll/LlReservedRefused_test.cpp)
cursiv_test(LlGuards_test "${_guard_cases}"
    rcondllGenerated rcondnestGenerated termcondlrGenerated
    refusedllGenerated refusedlrGenerated refusedmemollGenerated
    refusedmemolrGenerated refusedcondllGenerated cursivLlGen cursivLrGen)
set_target_properties(LlSemantic_test LlExpression_test LlGuards_test
    PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)
