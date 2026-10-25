cursiv_test(PerfCounters_test bench/PerfCounters_test.cpp cursiv::bench)

cursiv_test(ServiceMeasurement_test bench/ServiceMeasurement_test.cpp cursiv::bench)
cursiv_test(BenchmarkReferences_test bench/BenchmarkReferences_test.cpp cursiv::bench wsLsp)

cursiv_test(CliUtils_test cli/CliUtils_test.cpp cursiv::cli wsCore)
target_compile_definitions(CliUtils_test PRIVATE
    CURSIV_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")

cursiv_test(ClassItem_test core/text/ClassItem_test.cpp)
cursiv_test(LowerCase_test core/text/LowerCase_test.cpp)

cursiv_test(grammar_test core/grammar/grammar_test.cpp duallrGenerated dualllGenerated spellplainGenerated
    spelldashedGenerated sentblobGenerated sentwordGenerated sentfreeGenerated derivebaseGenerated
    derivedialectGenerated deriveruleGenerated deriveguardbaseGenerated deriveguarddialectGenerated
    derivetextdialectGenerated derivetextdialectllGenerated docswideGenerated docswidellGenerated
    cursivLrGen)
cursiv_grammar(docswide engines/lr/WideDocsGrammar.hpp tdocs::G ENGINE LR)
cursiv_grammar(docswidell engines/lr/WideDocsGrammar.hpp tdocs::Ll ENGINE LL)
cursiv_grammar(derivebase core/grammar/DerivedGrammar.hpp tderive::Base)
cursiv_grammar(derivedialect core/grammar/DerivedGrammar.hpp tderive::Dialect BASE derivebase)
cursiv_grammar(deriverule core/grammar/DerivedGrammar.hpp tderive::RuleDialect BASE derivebase)
cursiv_grammar(deriveguardbase core/grammar/DerivedGrammar.hpp tguard::Base)
cursiv_grammar(deriveguarddialect core/grammar/DerivedGrammar.hpp tguard::Dialect BASE deriveguardbase)
cursiv_grammar(derivetextdialect core/grammar/DerivedGrammar.hpp ttext::Dialect)
cursiv_grammar(derivetextdialectll core/grammar/DerivedGrammar.hpp ttext::DialectLl ENGINE LL)

cursiv_grammar_pair(dual core/syntax-tree/DualGrammar.hpp tdual::Lr tdual::Ll)
cursiv_test(threads_test core/threads_test.cpp)
cursiv_test(FunctionRef_test core/utils/FunctionRef_test.cpp)
cursiv_test(TypeList_test core/utils/TypeList_test.cpp)
target_compile_options(TypeList_test PRIVATE -ftemplate-depth=100)
cursiv_test(utils_test core/utils/utils_test.cpp wsCore)
# Replaces the global allocator, which the thread sanitizer's runtime also
# defines: the two do not link.
if(NOT CMAKE_CXX_FLAGS MATCHES "sanitize=thread")
  cursiv_test(PublicationFailure_test core/execution/PublicationFailure_test.cpp)
  set_tests_properties(PublicationFailure_test PROPERTIES TIMEOUT 30)
  set_target_properties(PublicationFailure_test PROPERTIES CURSIV_OWN_ALLOCATOR ON)
endif()

# The grammars a core lane parses with, one generated language each: a lane
# that needs a parser links the languages it names and instantiates nothing.
cursiv_grammar(conv core/syntax-tree/ConvertGrammars.hpp tconv::G)
cursiv_grammar(convll core/syntax-tree/ConvertGrammars.hpp tconv::Ll ENGINE LL)
# Small languages the lanes read through the parser, one case each.
cursiv_grammar(door engines/lr/DoorGrammar.hpp tdoor::G ENGINE LR)
# ONE LANGUAGE, TWO ENGINES: the same grammar generated twice, so a lane can
# hold both modules and compare what they wrote.
# These parsers also check shared state across generated translation units.
cursiv_grammar_pair(parity engines/ll/ParityGrammar.hpp tpar::Lr tpar::Ll
    SEPARATE_UNITS)
cursiv_grammar_pair(menu engines/ll/MenuGrammar.hpp tmenu::Lr tmenu::Ll)
cursiv_grammar_pair(tag engines/ll/TagGrammar.hpp ttag::Lr ttag::Ll)
cursiv_grammar(passes engines/lr/PassesGrammar.hpp tpasses::G ENGINE LR)
cursiv_grammar(astfusedlr engines/lr/AstFusedGrammar.hpp tfused::Lr ENGINE LR)
# A namespace holding its own `cursiv` and `std`: each engine's units name
# both from the global scope. Its region is read by both engines, compared.
cursiv_grammar_pair(shadow engines/lr/ShadowedNamespaceGrammars.hpp
    tshadow::cursiv::std::Lr tshadow::cursiv::std::Ll)
cursiv_grammar(sentblob core/grammar/SentinelGrammars.hpp tsent::Blob)
cursiv_grammar(sentword core/grammar/SentinelGrammars.hpp tsent::Word)
cursiv_grammar(sentfree core/grammar/SentinelGrammars.hpp tsent::Free)
cursiv_grammar(spellplain core/grammar/SpellingGrammars.hpp tspell::Plain)
cursiv_grammar(spelldashed core/grammar/SpellingGrammars.hpp tspell::Dashed)
cursiv_grammar(cstlex core/syntax-tree/CstNodeGrammars.hpp tcstn::LexG ENGINE LR)
cursiv_grammar(cstinfix core/syntax-tree/CstNodeGrammars.hpp tcstn::InfixG ENGINE LR)

cursiv_grammar(abbare core/syntax-tree/AstBuilderGrammars.hpp tab::bare::G ENGINE LR)
cursiv_grammar(aball core/syntax-tree/AstBuilderGrammars.hpp tab::All ENGINE LR)
cursiv_test(syntax_tree_test core/syntax-tree/syntax_tree_test.cpp cursivLrGen
    abbareGenerated aballGenerated duallrGenerated dualllGenerated convGenerated convllGenerated
    cstlexGenerated cstinfixGenerated)
set_tests_properties(syntax_tree_test PROPERTIES TIMEOUT 30 PROCESSORS 4)
