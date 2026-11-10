# Structural fuzzing: documents generated from the grammar, each with the tree
# it states, compared with the tree the parser builds.
cursiv_grammar(infixentry fuzz/InfixEntryGrammar.hpp finfix::InfixEntryG)
cursiv_test(gen_fuzz_test fuzz/infix_entry.cpp infixentryGenerated asiGenerated)

# The recovery metric's instruments on cases built by hand.
cursiv_test(RepairMetric_test metric/RepairMetric_test.cpp domain-modelGenerated)
target_include_directories(RepairMetric_test PRIVATE ${PROJECT_SOURCE_DIR}/bench)
# Grammars built to force forks, merges, election and the version cap.
cursiv_grammar(torture torture/TortureGrammar.hpp tort::Torture ENGINE LR)
cursiv_grammar(optionals torture/OptionalsGrammar.hpp topt::OptionalsG ENGINE LR)
cursiv_grammar(election torture/ElectionGrammar.hpp telect::Election EXTENSIONS .dangling ENGINE LR)
file(GLOB CURSIV_TORTURE_SOURCES CONFIGURE_DEPENDS torture/*.cpp)

cursiv_test(torture_test "${CURSIV_TORTURE_SOURCES}" cursiv::lsp tortureGenerated
    electionGenerated optionalsGenerated)
set_target_properties(torture_test PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 3)
target_compile_definitions(torture_test PRIVATE
    CURSIV_TORTURE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/torture/corpus"
    CURSIV_RECOVERY_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/torture/corpus")
