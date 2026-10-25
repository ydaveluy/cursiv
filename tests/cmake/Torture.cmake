# The structural fuzz lane: documents written from the declared grammar, and
# the tree each one owes weighed against the tree the parser built.
cursiv_grammar(infixentry fuzz/InfixEntryGrammar.hpp finfix::InfixEntryG)
cursiv_test(gen_fuzz_test fuzz/infix_entry.cpp infixentryGenerated)

# The recovery metric's instruments on cases built by hand.
cursiv_test(RepairMetric_test metric/RepairMetric_test.cpp domain-modelGenerated)
target_include_directories(RepairMetric_test PRIVATE ${PROJECT_SOURCE_DIR}/bench)
# The GLR/ambiguity torture lane: grammars built to force forks, merges,
# election and the version cap -- a findings report, nothing here ships.
cursiv_grammar(torture torture/TortureGrammar.hpp tort::Torture ENGINE LR)
cursiv_grammar(dangling torture/DeepIfGrammar.hpp tdang::DeepIf EXTENSIONS .dangling ENGINE LR)
cursiv_grammar(optionals torture/OptionalsGrammar.hpp topt::OptionalsG ENGINE LR)
cursiv_grammar(election torture/ElectionGrammar.hpp telect::Election ENGINE LR)
# A literal a grammar may legally spell and the emitter must survive: a raw
# newline inside a C++ string is not a string.
cursiv_grammar(newline engines/lr/NewlineGrammar.hpp tnl::NlG ENGINE LR)
cursiv_grammar(boundary engines/lr/BoundaryGrammar.hpp tbnd::BoundaryG ENGINE LR)
file(GLOB CURSIV_TORTURE_SOURCES CONFIGURE_DEPENDS torture/*.cpp)

cursiv_test(torture_test "${CURSIV_TORTURE_SOURCES}" cursiv::lsp tortureGenerated danglingGenerated
    electionGenerated optionalsGenerated newlineGenerated boundaryGenerated)
target_compile_definitions(torture_test PRIVATE
    CURSIV_TORTURE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/torture/corpus"
    CURSIV_RECOVERY_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/torture/corpus")
