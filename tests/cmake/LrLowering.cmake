# The construction, piece by piece: action and close rows, programs, ordered
# reduces, the stamp split and the lowering's limits.
cursiv_test(LrConstruction_test engines/lr/LrConstruction_test.cpp
    cursivLrGen)
cursiv_grammar(edges engines/lr/EdgesGrammar.hpp tedges::G ENGINE LR)
# Where a reading commits and where a token ends: committed repetitions, region
# edges and guarded alternatives.
cursiv_test(LrEdges_test engines/lr/LrEdges_test.cpp
    edgesGenerated
    skiplrGenerated
    wideoptionallrGenerated
    sectiontaillrGenerated
    sectiontailllGenerated)
# Regions naming trivia of their own.
cursiv_grammar(skiplr engines/lr/RegionSkipGrammar.hpp tskip::Lr ENGINE LR)
# Twenty optional positions on a region's edge or an end assertion.
cursiv_grammar(wideoptionallr engines/lr/WideOptionalGrammar.hpp twide::Lr ENGINE LR)
# Embedded grammars in both engines, with one standalone JSON oracle.
cursiv_grammar_pair(embed engines/lr/EmbedGrammar.hpp tembed::DocLr tembed::DocLl)
cursiv_grammar(embedjsonlr engines/lr/EmbedGrammar.hpp tembed::JsonLr ENGINE LR)
cursiv_test(Embed_test engines/lr/Embed_test.cpp
    embedlrGenerated
    embedllGenerated
    embedjsonlrGenerated)
cursiv_grammar_pair(sectiontail engines/lr/SectionTailGrammar.hpp tsection::Lr tsection::Ll)
cursiv_grammar(lraudit engines/lr/AuditGrammar.hpp taudit::G ENGINE LR)
cursiv_grammar(peekcaret engines/lr/PeekCaretGrammar.hpp tpeekcaret::G ENGINE LR)
# States a table keeps apart: peeks, virtual readings, reduce cells, and the
# states split in groups.
cursiv_test(LrStates_test engines/lr/LrStates_test.cpp
    peekcaretGenerated
    caretspliceGenerated
    widetermsGenerated
    indentGenerated
    groupguardGenerated
    carettriviaGenerated
    bracketcaretGenerated
    continuedlrGenerated
    rulerefusalGenerated)
cursiv_grammar(caretsplice engines/lr/PeekCaretGrammar.hpp tpeekcaret::Splice ENGINE LR)
# The cases of the passes grammar: construction passes, ranked cells and soft
# keywords.
cursiv_test(LrPasses_test engines/lr/LrPasses_test.cpp
    passesGenerated
    doorGenerated)
cursiv_grammar(deferrednested engines/lr/DeferredChoiceGrammars.hpp tdeferred::NestedFirst ENGINE LR)
# Only the master project builds what the shipped languages' own documents ask.
if(cursiv_IS_TOP_LEVEL)
  # The shipped examples under repair and at the caret.
  cursiv_test(LrExamples_test engines/lr/LrExamples_test.cpp
    arithmeticsCore
    domainmodelCore
    requirementsCore
    statemachineCore
    domain-modelGenerated
    arithmeticsGenerated
    asicasesGenerated
    unsettledGenerated)
  target_compile_definitions(LrExamples_test PRIVATE
      CURSIV_EXAMPLE_DIR="${PROJECT_SOURCE_DIR}/examples")

  # Which repair a reading makes, what the search opens to find it, and what
  # it costs over a long document.
  cursiv_grammar(opentoken engines/lr/OpenTokenGrammar.hpp topen::G ENGINE LR)
  # The repair search against an enumeration of its gestures, and its
  # writer against the drive over whole documents.
  cursiv_test(LrRepairSearch_test engines/lr/LrRepairSearch_test.cpp
    arithmeticsGenerated
    domain-modelGenerated
    indentGenerated
    ambtailGenerated
    danglingGenerated
    astfusedlrGenerated
    opentokenGenerated
    asiGenerated
    repaircostGenerated
    paritylrGenerated
    asicasesGenerated
    nestedGenerated)

  # A parse past its repair budget ends, accepted, whatever it met.
  cursiv_grammar(owed engines/lr/OwedLoopGrammars.hpp towed::G ENGINE LR)
  cursiv_grammar(degraded engines/lr/DegradedGrammars.hpp tdegraded::G ENGINE LR)
  cursiv_test(LrRepairBudget_test engines/lr/LrRepairBudget_test.cpp
    arithmeticsGenerated
    asiGenerated
    requirements-langGenerated
    degradedGenerated
    owedGenerated)
  # Its CPU work has a 30s watchdog: reserve all four CI test slots.
  set_tests_properties(LrRepairBudget_test PROPERTIES TIMEOUT 60 PROCESSORS 4)
endif()
# Grammars both lowered in process and generated: what the lowering keeps is
# what the generated parser reads.
# The small languages of the door grammar, read through the parser.
# Grammars lowered in process: where an infix step rides, how a directive
# distributes, and the boundary a region hands back.
cursiv_test(LrLowered_test engines/lr/LrLowered_test.cpp
    cursivLrGen
    doorGenerated
    passesGenerated
    asicasesGenerated)
