cursiv_test(bench_test bench/bench_test.cpp cursiv::bench wsLsp)

cursiv_test(CliUtils_test cli/CliUtils_test.cpp cursiv::cli cursiv::lsp wsCore)
target_compile_definitions(CliUtils_test PRIVATE
    CURSIV_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")

cursiv_test(text_test core/text/text_test.cpp cursiv::gen)

cursiv_test(grammar_test core/grammar/grammar_test.cpp cursiv::lsp cursiv::gen duallrGenerated spellplainGenerated
    spelldashedGenerated sentblobGenerated sentwordGenerated sentfreeGenerated derivebaseGenerated
    derivedialectGenerated deriveruleGenerated
    derivetextdialectGenerated docswideGenerated)
# It reads the names of the types it declares, as a grammar's text spells them.
set_target_properties(grammar_test PROPERTIES CURSIV_TEST_ALONE ON)
cursiv_grammar(docswide grammars/WideDocsGrammar.hpp tdocs::G)
cursiv_grammar(derivebase core/grammar/DerivedGrammar.hpp tderive::Base)
cursiv_grammar(derivedialect core/grammar/DerivedGrammar.hpp tderive::Dialect BASE derivebase)
cursiv_grammar(deriverule core/grammar/DerivedGrammar.hpp tderive::RuleDialect BASE derivebase)
cursiv_grammar(derivetextdialect core/grammar/DerivedGrammar.hpp ttext::Dialect)

cursiv_grammar(duallr core/syntax-tree/DualGrammar.hpp tdual::D2)
cursiv_test(threads_test core/threads_test.cpp cursiv::gen)
cursiv_test(TypeList_test core/utils/TypeList_test.cpp)
target_compile_options(TypeList_test PRIVATE -ftemplate-depth=100)
cursiv_test(utils_test core/utils/utils_test.cpp cursiv::lsp wsCore)
# It instantiates a template of the library explicitly, at global scope.
set_target_properties(utils_test PROPERTIES CURSIV_TEST_ALONE ON)
# Replaces the global allocator, which the thread sanitizer's runtime also
# defines: the two do not link.
if(NOT CMAKE_CXX_FLAGS MATCHES "sanitize=thread")
  cursiv_test(AllocationFailure_test core/AllocationFailure_test.cpp wsCore
      convGenerated)
  cursiv_timeout(AllocationFailure_test 30)
  cursiv_own_allocator(AllocationFailure_test)
endif()

cursiv_grammar(conv core/syntax-tree/ConvertGrammars.hpp tconv::G)
cursiv_grammar(smalllanguages grammars/SmallLanguagesGrammar.hpp tsmall::G)
# Generated in separate units, so its tests also check the state they share.
cursiv_grammar(paritylr grammars/ParityGrammar.hpp tpar::Suite
    SEPARATE_UNITS)
cursiv_grammar(menulr grammars/MenuGrammar.hpp tmenu::Base)
cursiv_grammar(taglr grammars/TagGrammar.hpp ttag::Base)
cursiv_grammar(passes grammars/PassesGrammar.hpp tpasses::G)
cursiv_grammar(astfusedlr grammars/AstFusedGrammar.hpp tfused::Lr)
cursiv_grammar(indent grammars/IndentGrammar.hpp tindent::G)
cursiv_grammar(indentfolded grammars/IndentGrammar.hpp tindent::folded::Cases)
cursiv_grammar(indentrepaired grammars/IndentGrammar.hpp tindent::repaired::G)
cursiv_grammar(indentimplied grammars/IndentGrammar.hpp tindent::synthetic::Cases)
cursiv_test(Indent_test grammars/Indent_test.cpp
    indentGenerated
    indentfoldedGenerated
    indentrepairedGenerated
    indentimpliedGenerated)
# A namespace holding its own `cursiv` and `std`: generated code must name both
# from the global scope.
cursiv_grammar(shadowlr grammars/ShadowedNamespaceGrammars.hpp
    tshadow::cursiv::std::Base)
cursiv_grammar(sentblob core/grammar/SentinelGrammars.hpp tsent::Blob)
cursiv_grammar(sentword core/grammar/SentinelGrammars.hpp tsent::Word)
cursiv_grammar(sentfree core/grammar/SentinelGrammars.hpp tsent::Free)
cursiv_grammar(spellplain core/grammar/SpellingGrammars.hpp tspell::Plain)
cursiv_grammar(spelldashed core/grammar/SpellingGrammars.hpp tspell::Dashed)
cursiv_grammar(cstlex core/syntax-tree/CstNodeGrammars.hpp tcstn::LexG)
cursiv_grammar(cstinfix core/syntax-tree/CstNodeGrammars.hpp tcstn::InfixG)

cursiv_grammar(abbare core/syntax-tree/AstBuilderGrammars.hpp tab::bare::G)
cursiv_grammar(aball core/syntax-tree/AstBuilderGrammars.hpp tab::All)
cursiv_test(syntax_tree_test core/syntax-tree/syntax_tree_test.cpp cursiv::lsp
    abbareGenerated aballGenerated duallrGenerated convGenerated
    cstlexGenerated cstinfixGenerated)
set_tests_properties(syntax_tree_test PROPERTIES TIMEOUT 30 PROCESSORS 4)
