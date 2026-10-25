# A cell read ahead across an empty reduction and a backslash continuation.
cursiv_grammar(continuedlr engines/lr/ContinuedLineGrammar.hpp tcontinued::Lr ENGINE LR)
cursiv_grammar(guardfallback engines/lr/GuardFallbackGrammar.hpp tfallback::G ENGINE LR)
cursiv_grammar(unsettled engines/lr/UnsettledCellGrammar.hpp tunsettled::G ENGINE LR)
cursiv_grammar(ambtail engines/lr/AmbiguousTailGrammar.hpp tambtail::G ENGINE LR)
cursiv_grammar(backreflua engines/lr/BackrefGrammars.hpp tbackref::lua::G ENGINE LR)
cursiv_grammar(backref engines/lr/BackrefGrammars.hpp tbackref::G ENGINE LR)
cursiv_grammar(nested engines/lr/LexNestedGrammars.hpp tnestedlex::G ENGINE LR)
cursiv_grammar(relayoverlap engines/lr/LexNestedGrammars.hpp tnestedlex::overlap::G ENGINE LR)
cursiv_test(LexRelay_test engines/lr/LexRelay_test.cpp
    cursivLrGen
    backrefGenerated
    backrefluaGenerated
    nestedGenerated
    relayoverlapGenerated)
# Bounds the runs a closer search or a relay read per opener would never
# finish.
set_tests_properties(LexRelay_test PROPERTIES TIMEOUT 120)
cursiv_grammar(cycle engines/lr/CycleGrammar.hpp tcycle::G ENGINE LR)
# Bounds the run a walk restarted at every byte would never finish.
cursiv_test(LrGuardedChoice_test engines/lr/LrGuardedChoice_test.cpp
    groupguardGenerated)
cursiv_grammar(carettrivia engines/lr/CaretTriviaGrammar.hpp tcarettrivia::G ENGINE LR)
cursiv_grammar(bracketcaret engines/lr/BracketCaretGrammar.hpp tbracketcaret::G ENGINE LR)
cursiv_grammar(indent engines/lr/IndentGrammar.hpp tindent::G ENGINE LR)
cursiv_grammar(indentfolded engines/lr/IndentGrammar.hpp tindent::folded::Cases ENGINE LR)
cursiv_grammar(indentrepaired engines/lr/IndentGrammar.hpp tindent::repaired::G ENGINE LR)
cursiv_grammar(indentimplied engines/lr/IndentGrammar.hpp tindent::laid::Cases ENGINE LR)
cursiv_grammar(adopt engines/lr/AdoptEntryGrammar.hpp tadopt::G ENGINE LR)
cursiv_grammar(unicodewide engines/lr/LexUnicodeGrammars.hpp tunicode::wide::G ENGINE LR)
cursiv_grammar(rulerefusal engines/lr/RuleRefusalGrammars.hpp trefuse::head::G ENGINE LR)
cursiv_grammar(wideterms engines/lr/WideTerminalsGrammar.hpp twide::G ENGINE LR)
# Lookaheads on the token in hand, and past it as far as a completion reads.
cursiv_test(LrRuleRefusal_test engines/lr/LrRuleRefusal_test.cpp
    rulerefusalGenerated)
cursiv_grammar(asi engines/lr/AsiGrammar.hpp tasi::G ENGINE LR GENERATOR_ARGS --group-lines=64)
cursiv_grammar(asicases engines/lr/AsiGrammar.hpp tasi::cases::G ENGINE LR)
cursiv_test(LrAsi_test engines/lr/LrAsi_test.cpp
    asiGenerated
    asicasesGenerated)
cursiv_grammar(groupguard engines/lr/GroupGuardGrammar.hpp tgroupguard::G ENGINE LR GENERATOR_ARGS --group-lines=64)
cursiv_test(LrIndent_test engines/lr/LrIndent_test.cpp
    indentGenerated
    indentfoldedGenerated
    indentrepairedGenerated
    indentimpliedGenerated)
cursiv_grammar(cells engines/lr/CellWidthGrammars.hpp tcells::G ENGINE LR)
# Cells and positions no look-ahead settles, split rather than guessed.
cursiv_test(LrEngine_test engines/lr/LrEngine_test.cpp
    shadowllGenerated
    shadowlrGenerated
    guardfallbackGenerated
    edgesGenerated
    unsettledGenerated
    cellsGenerated
    cursivLrGen
    cycleGenerated
    refusedheadsGenerated
    nestedGenerated
    lexicalpredicateslrGenerated)
# The construction itself, which is a host tool: its storage reserves address
# space wasm32 does not offer, so a cross declares no lane that runs it.
if(NOT CMAKE_CROSSCOMPILING)
  # The construction run natively: the identity it picks and the completion
  # sites it agrees on.
  cursiv_test(LrIdentity_test engines/lr/LrIdentity_test.cpp
    cursiv::lr_gen
    astfusedlrGenerated
    passesGenerated
    deferrednestedGenerated)
endif()

# Shared lexical refusal fixture, retained here after its LL suite was consolidated.
cursiv_grammar(shorterlr engines/lr/RefusedShorterGrammar.hpp trefusedshorter::Lr ENGINE LR)

# The LR failed-walk oracle uses the shared lexical-predicate fixture.
cursiv_grammar(lexicalpredicateslr engines/ll/LexicalPredicatesGrammar.hpp tlexicalpredicates::Lr ENGINE LR)

# Raw failed-walk oracles retain the original standalone lexer modes.
cursiv_grammar(refusedheads engines/lr/RefusedHeadWordsGrammar.hpp trefusedheads::G ENGINE LR)
