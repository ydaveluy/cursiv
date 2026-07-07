#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pegium/core/CoreTestSupport.hpp>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/syntax-tree/AstReflection.hpp>
#include <pegium/core/validation/DefaultValidationRegistry.hpp>
#include <pegium/core/validation/DiagnosticRanges.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>
#include <pegium/core/workspace/Document.hpp>

#include "ValidationTestUtils.hpp"

namespace pegium::validation {
namespace {

using namespace pegium::parser;
using test::ValidationNodeA;
using test::ValidationNodeB;

struct ValidationRoot : pegium::AstNode {
  vector<pointer<ValidationNodeA>> nodes;
};

struct BoundValidator {
  std::shared_ptr<std::size_t> nodeACalls = std::make_shared<std::size_t>(0);
  std::shared_ptr<std::size_t> nodeBCalls = std::make_shared<std::size_t>(0);

  void checkNodeA(const ValidationNodeA &, const ValidationAcceptor &) const {
    ++*nodeACalls;
  }

  void checkNodeB(const ValidationNodeB &, const ValidationAcceptor &) const {
    ++*nodeBCalls;
  }
};

struct MoveOnlyValidator {
  explicit MoveOnlyValidator(std::unique_ptr<std::size_t> calls)
      : calls(std::move(calls)) {}

  MoveOnlyValidator(const MoveOnlyValidator &) = delete;
  MoveOnlyValidator &operator=(const MoveOnlyValidator &) = delete;
  MoveOnlyValidator(MoveOnlyValidator &&) noexcept = default;
  MoveOnlyValidator &operator=(MoveOnlyValidator &&) noexcept = default;

  void checkNodeA(const ValidationNodeA &, const ValidationAcceptor &) const {
    ++*calls;
  }

  std::unique_ptr<std::size_t> calls;
};

struct ValidationBaseNode : pegium::AstNode {};
struct ValidationDerivedNode : ValidationBaseNode {};

class ValidationSubtypeBootstrapParser final : public PegiumParser {
protected:
  const grammar::ParserRule &getEntryRule() const noexcept override {
    return BaseRule;
  }

  Rule<ValidationDerivedNode> DerivedRule{"Derived", "derived"_kw};
  Rule<ValidationBaseNode> BaseRule{"Base", DerivedRule};
};

// An abstract C++ base the grammar never names: only the concrete leaf is a
// rule result, so reflection knows the leaf but not the base.
struct UnnamedAbstractBaseNode : pegium::AstNode {};
struct GrammarProducedLeafNode : UnnamedAbstractBaseNode {};

class UnnamedBaseBootstrapParser final : public PegiumParser {
protected:
  const grammar::ParserRule &getEntryRule() const noexcept override {
    return LeafRule;
  }

  Rule<GrammarProducedLeafNode> LeafRule{"Leaf", "leaf"_kw};
};

// Two produced types related by C++ inheritance but produced by sibling rules
// that never nest one where the other is expected (the Class/Exception shape).
// Both are known to reflection, yet the grammar induces no edge between them.
struct SiblingBaseNode : pegium::AstNode {};
struct SiblingDerivedNode : SiblingBaseNode {};
struct SiblingRoot : pegium::AstNode {
  vector<pointer<SiblingBaseNode>> bases;
  vector<pointer<SiblingDerivedNode>> deriveds;
};

class SiblingRuleBootstrapParser final : public PegiumParser {
protected:
  const grammar::ParserRule &getEntryRule() const noexcept override {
    return RootRule;
  }

  Rule<SiblingBaseNode> BaseRule{"Base", "base"_kw};
  Rule<SiblingDerivedNode> DerivedRule{"Derived", "derived"_kw};
  Rule<SiblingRoot> RootRule{
      "Root", append<&SiblingRoot::bases>(BaseRule) +
                  append<&SiblingRoot::deriveds>(DerivedRule)};
};

class ValidationNodeABootstrapParser final : public PegiumParser {
protected:
  const grammar::ParserRule &getEntryRule() const noexcept override {
    return RootRule;
  }

  Rule<ValidationNodeA> NodeRule{"Node", "node"_kw};
  Rule<ValidationRoot> RootRule{"Root", append<&ValidationRoot::nodes>(NodeRule)};
};

struct RegistryTestServices {
  RegistryTestServices() : core(shared) {
    shared.observabilitySink = observabilitySink;
    shared.astReflection = std::make_unique<pegium::AstReflection>();
  }

  std::shared_ptr<test::RecordingObservabilitySink> observabilitySink =
      std::make_shared<test::RecordingObservabilitySink>();
  pegium::SharedCoreServices shared;
  pegium::CoreServices core;
};

void run_checks(const ValidationRegistry &registry, const AstNode &node,
                std::span<const std::string> categories,
                const ValidationAcceptor &acceptor,
                const utils::CancellationToken &cancelToken = {}) {
  registry.runChecks(node, acceptor, categories, cancelToken);
}

// Folds the run/dispatch family: every test here registers typed
// registerCheck<T>(counterLambda, category) checks and asserts per-counter call
// totals after a sequence of runChecks(node, acceptor, categories) calls.
// Folded originals (one row each):
//   - FiltersChecksByTypeAndCategory
//   - LateRegistrationsArePickedUpOnNextRun
//   - RunChecksKeepsTypeSpecificMatchesAcrossAlternatingNodes
//   - PreparedChecksWithDifferentCategoriesStayIndependent
//   - UnknownCategoriesExecuteNothing
TEST(DefaultValidationRegistryTest, TypeAndCategoryDispatch) {
  enum class NodeKind { A, B };

  // A single scenario step: either register a new typed check bound to a named
  // counter, or run a node under given categories and snapshot expected totals.
  struct Step {
    bool isRun = false;
    // register fields
    NodeKind regKind = NodeKind::A;
    bool regAst = false; // register a base-AstNode check instead of NodeKind
    std::string counter;
    std::string category;
    // run fields
    NodeKind runKind = NodeKind::A;
    std::vector<std::string> runCategories;
    std::vector<std::pair<std::string, std::size_t>> expected;
  };

  auto reg = [](NodeKind kind, std::string counter, std::string category) {
    return Step{.isRun = false,
                .regKind = kind,
                .counter = std::move(counter),
                .category = std::move(category)};
  };
  auto regAst = [](std::string counter, std::string category) {
    return Step{.isRun = false,
                .regAst = true,
                .counter = std::move(counter),
                .category = std::move(category)};
  };
  auto run = [](NodeKind kind, std::vector<std::string> categories,
                std::vector<std::pair<std::string, std::size_t>> expected) {
    return Step{.isRun = true,
                .runKind = kind,
                .runCategories = std::move(categories),
                .expected = std::move(expected)};
  };

  struct Scenario {
    const char *label;
    bool needsBootstrap;
    std::vector<Step> steps;
  };

  const std::vector<Scenario> scenarios{
      {"FiltersChecksByTypeAndCategory",
       /*needsBootstrap=*/true,
       {reg(NodeKind::A, "a", "fast"), reg(NodeKind::B, "b", "fast"),
        regAst("astSlow", "slow"),
        run(NodeKind::A, {"fast"},
            {{"a", 1u}, {"b", 0u}, {"astSlow", 0u}}),
        run(NodeKind::A, {},
            {{"a", 2u}, {"b", 0u}, {"astSlow", 1u}})}},
      {"LateRegistrationsArePickedUpOnNextRun",
       /*needsBootstrap=*/false,
       {reg(NodeKind::A, "existing", "fast"),
        run(NodeKind::A, {}, {{"existing", 1u}, {"late", 0u}}),
        reg(NodeKind::A, "late", "fast"),
        run(NodeKind::A, {}, {{"existing", 2u}, {"late", 1u}})}},
      {"RunChecksKeepsTypeSpecificMatchesAcrossAlternatingNodes",
       /*needsBootstrap=*/false,
       {reg(NodeKind::A, "a", "fast"), reg(NodeKind::B, "b", "fast"),
        run(NodeKind::A, {}, {}), run(NodeKind::B, {}, {}),
        run(NodeKind::A, {}, {{"a", 2u}, {"b", 1u}})}},
      {"PreparedChecksWithDifferentCategoriesStayIndependent",
       /*needsBootstrap=*/false,
       {reg(NodeKind::A, "fast", "fast"), reg(NodeKind::A, "slow", "slow"),
        run(NodeKind::A, {"fast"}, {}),
        run(NodeKind::A, {"slow"}, {{"fast", 1u}, {"slow", 1u}})}},
      {"UnknownCategoriesExecuteNothing",
       /*needsBootstrap=*/false,
       {reg(NodeKind::A, "calls", "fast"),
        run(NodeKind::A, {"unknown"}, {{"calls", 0u}})}},
  };

  for (const auto &scenario : scenarios) {
    SCOPED_TRACE(scenario.label);
    RegistryTestServices context;
    DefaultValidationRegistry registry(context.core);
    if (scenario.needsBootstrap) {
      ValidationNodeABootstrapParser parser;
      bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                             *context.shared.astReflection);
    }

    auto counters = std::make_shared<std::map<std::string, std::size_t>>();
    auto noopAcceptorCb = [](pegium::Diagnostic) {};
    const ValidationAcceptor noopAcceptor{
        ValidationAcceptor::Callback(noopAcceptorCb)};
    ValidationNodeA nodeA;
    ValidationNodeB nodeB;

    for (const auto &step : scenario.steps) {
      if (!step.isRun) {
        const std::string name = step.counter;
        if (step.regAst) {
          registry.registerCheck<pegium::AstNode>(
              [counters, name](const pegium::AstNode &,
                               const ValidationAcceptor &) { ++(*counters)[name]; },
              step.category);
        } else if (step.regKind == NodeKind::A) {
          registry.registerCheck<ValidationNodeA>(
              [counters, name](const ValidationNodeA &,
                               const ValidationAcceptor &) { ++(*counters)[name]; },
              step.category);
        } else {
          registry.registerCheck<ValidationNodeB>(
              [counters, name](const ValidationNodeB &,
                               const ValidationAcceptor &) { ++(*counters)[name]; },
              step.category);
        }
        continue;
      }

      const pegium::AstNode &node =
          step.runKind == NodeKind::A ? static_cast<pegium::AstNode &>(nodeA)
                                      : static_cast<pegium::AstNode &>(nodeB);
      registry.runChecks(node, noopAcceptor, step.runCategories, {});
      for (const auto &[name, value] : step.expected) {
        SCOPED_TRACE(name);
        EXPECT_EQ((*counters)[name], value);
      }
    }
  }
}

TEST(DefaultValidationRegistryTest, RejectsBuiltInCustomCategory) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);

  EXPECT_THROW(
      registry.registerCheck<ValidationNodeA>(
          [](const ValidationNodeA &, const ValidationAcceptor &) {},
          std::string(kBuiltInValidationCategory)),
      std::invalid_argument);
}

TEST(DefaultValidationRegistryTest,
     LateRegistrationsUpdateValidationCategories) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);

  registry.registerCheck<ValidationNodeA>(
      [](const ValidationNodeA &, const ValidationAcceptor &) {}, "fast");

  EXPECT_EQ(registry.getAllValidationCategories(),
            (std::vector<std::string>{"fast", "slow", "built-in"}));

  registry.registerCheck<ValidationNodeA>(
      [](const ValidationNodeA &, const ValidationAcceptor &) {}, "project");

  EXPECT_EQ(
      registry.getAllValidationCategories(),
      (std::vector<std::string>{"fast", "slow", "built-in", "project"}));
}

TEST(DefaultValidationRegistryTest, StoresBeforeAndAfterDocumentHooks) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);

  std::size_t beforeCalls = 0;
  std::size_t afterCalls = 0;

  registry.registerBeforeDocument(
      [&beforeCalls](const pegium::AstNode &, const ValidationAcceptor &,
                     std::span<const std::string>,
                     const utils::CancellationToken &) { ++beforeCalls; });
  registry.registerAfterDocument(
      [&afterCalls](const pegium::AstNode &, const ValidationAcceptor &,
                    std::span<const std::string>,
                    const utils::CancellationToken &) { ++afterCalls; });

  ASSERT_EQ(registry.checksBefore().size(), 1u);
  ASSERT_EQ(registry.checksAfter().size(), 1u);

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationNodeA root;
  const std::vector<std::string> categories{"fast"};

  registry.checksBefore().front()(root, noopAcceptor, categories, {});
  registry.checksAfter().front()(root, noopAcceptor, categories, {});

  EXPECT_EQ(beforeCalls, 1u);
  EXPECT_EQ(afterCalls, 1u);
}

TEST(DefaultValidationRegistryTest, LateRegistrationsUpdateHookReads) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  std::vector<std::string> order;

  registry.registerBeforeDocument(
      [&order](const pegium::AstNode &, const ValidationAcceptor &,
               std::span<const std::string>,
               const utils::CancellationToken &) { order.push_back("before-1"); });
  registry.registerAfterDocument(
      [&order](const pegium::AstNode &, const ValidationAcceptor &,
               std::span<const std::string>,
               const utils::CancellationToken &) { order.push_back("after-1"); });

  ASSERT_EQ(registry.checksBefore().size(), 1u);
  ASSERT_EQ(registry.checksAfter().size(), 1u);

  registry.registerBeforeDocument(
      [&order](const pegium::AstNode &, const ValidationAcceptor &,
               std::span<const std::string>,
               const utils::CancellationToken &) { order.push_back("before-2"); });
  registry.registerAfterDocument(
      [&order](const pegium::AstNode &, const ValidationAcceptor &,
               std::span<const std::string>,
               const utils::CancellationToken &) { order.push_back("after-2"); });

  ASSERT_EQ(registry.checksBefore().size(), 2u);
  ASSERT_EQ(registry.checksAfter().size(), 2u);

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationNodeA root;
  const std::vector<std::string> categories{"fast"};

  for (const auto &checkBefore : registry.checksBefore()) {
    checkBefore(root, noopAcceptor, categories, {});
  }
  for (const auto &checkAfter : registry.checksAfter()) {
    checkAfter(root, noopAcceptor, categories, {});
  }

  EXPECT_EQ(order, (std::vector<std::string>{
                       "before-1",
                       "before-2",
                       "after-1",
                       "after-2",
                   }));
}

TEST(DefaultValidationRegistryTest, RegisterChecksAddsGroupedTypedChecks) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  std::size_t nodeACalls = 0;
  std::size_t nodeBCalls = 0;

  registry.registerChecks(
      {ValidationRegistry::makeValidationCheck<ValidationNodeA>(
           [&nodeACalls](const ValidationNodeA &, const ValidationAcceptor &) {
             ++nodeACalls;
           }),
       ValidationRegistry::makeValidationCheck<ValidationNodeB>(
           [&nodeBCalls](const ValidationNodeB &, const ValidationAcceptor &) {
             ++nodeBCalls;
           })},
      "fast");

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationNodeA nodeA;
  ValidationNodeB nodeB;

  run_checks(registry, nodeA, {}, noopAcceptor);
  run_checks(registry, nodeB, {}, noopAcceptor);

  EXPECT_EQ(nodeACalls, 1u);
  EXPECT_EQ(nodeBCalls, 1u);
}

TEST(DefaultValidationRegistryTest, RegisterChecksBindsValidatorMethods) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  const BoundValidator validator;

  registry.registerChecks(
      {ValidationRegistry::makeValidationCheck<&BoundValidator::checkNodeA>(
           validator),
       ValidationRegistry::makeValidationCheck<&BoundValidator::checkNodeB>(
           validator)},
      "fast");

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationNodeA nodeA;
  ValidationNodeB nodeB;

  run_checks(registry, nodeA, {}, noopAcceptor);
  run_checks(registry, nodeB, {}, noopAcceptor);

  EXPECT_EQ(*validator.nodeACalls, 1u);
  EXPECT_EQ(*validator.nodeBCalls, 1u);
}

TEST(DefaultValidationRegistryTest,
     RegisterChecksBindsMoveOnlyValidatorMethods) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  auto calls = std::make_unique<std::size_t>(0);
  auto *callCounter = calls.get();
  // The registry binds the validator by pointer (non-owning), so a move-only,
  // non-copyable validator works without being copied. It must outlive the
  // registry's checks, which it does here.
  const MoveOnlyValidator validator(std::move(calls));

  registry.registerChecks(
      {ValidationRegistry::makeValidationCheck<&MoveOnlyValidator::checkNodeA>(
          validator)},
      "fast");

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationNodeA nodeA;
  run_checks(registry, nodeA, {}, noopAcceptor);

  ASSERT_NE(callCounter, nullptr);
  EXPECT_EQ(*callCounter, 1u);
}

// Folds the "throwing stage becomes a diagnostic + observation" family: a
// throwing callable is registered for a stage, the stage is triggered, and the
// resulting single diagnostic message plus the recorded observation
// (code + message substring) are asserted.
// Folded originals (one row each):
//   - ValidationCheckExceptionsBecomeDiagnostics
//   - PreparationExceptionsBecomeDiagnostics
//   - FinalizationExceptionsBecomeDiagnosticsAndObservations
TEST(DefaultValidationRegistryTest, ThrowingStagesBecomeDiagnostics) {
  struct Scenario {
    const char *label;
    const char *thrown;
    const char *diagnosticSubstr;
    const char *observationSubstr;
    observability::ObservationCode code;
    bool expectErrorSeverity;
    // Registers the throwing callable for the stage, then triggers it once.
    void (*registerAndTrigger)(DefaultValidationRegistry &, const char *thrown,
                               const ValidationAcceptor &);
  };

  const std::vector<Scenario> scenarios{
      {"ValidationCheckExceptionsBecomeDiagnostics", "boom",
       "An error occurred during validation: boom", "boom",
       observability::ObservationCode::ValidationCheckThrew,
       /*expectErrorSeverity=*/true,
       [](DefaultValidationRegistry &registry, const char *thrown,
          const ValidationAcceptor &acceptor) {
         registry.registerCheck<ValidationNodeA>(
             [thrown](const ValidationNodeA &, const ValidationAcceptor &) {
               throw std::runtime_error(thrown);
             });
         ValidationNodeA node;
         registry.runChecks(node, acceptor, {}, {});
       }},
      {"PreparationExceptionsBecomeDiagnostics", "setup failed",
       "An error occurred during set-up of the validation: setup failed",
       "setup failed",
       observability::ObservationCode::ValidationPreparationThrew,
       /*expectErrorSeverity=*/true,
       [](DefaultValidationRegistry &registry, const char *thrown,
          const ValidationAcceptor &acceptor) {
         registry.registerBeforeDocument(
             [thrown](const pegium::AstNode &, const ValidationAcceptor &,
                      std::span<const std::string>,
                      const utils::CancellationToken &) {
               throw std::runtime_error(thrown);
             });
         ValidationNodeA root;
         registry.checksBefore().front()(root, acceptor, {}, {});
       }},
      {"FinalizationExceptionsBecomeDiagnosticsAndObservations",
       "tear-down failed",
       "An error occurred during tear-down of the validation: tear-down failed",
       "tear-down failed",
       observability::ObservationCode::ValidationFinalizationThrew,
       /*expectErrorSeverity=*/false,
       [](DefaultValidationRegistry &registry, const char *thrown,
          const ValidationAcceptor &acceptor) {
         registry.registerAfterDocument(
             [thrown](const pegium::AstNode &, const ValidationAcceptor &,
                      std::span<const std::string>,
                      const utils::CancellationToken &) {
               throw std::runtime_error(thrown);
             });
         ValidationNodeA root;
         registry.checksAfter().front()(root, acceptor, {}, {});
       }},
  };

  for (const auto &scenario : scenarios) {
    SCOPED_TRACE(scenario.label);
    RegistryTestServices context;
    DefaultValidationRegistry registry(context.core);

    std::vector<std::string> messages;
    const bool expectErrorSeverity = scenario.expectErrorSeverity;
    auto acceptorCb = [&messages,
                       expectErrorSeverity](pegium::Diagnostic diagnostic) {
      if (expectErrorSeverity) {
        EXPECT_EQ(diagnostic.severity, pegium::DiagnosticSeverity::Error);
      }
      messages.push_back(std::move(diagnostic.message));
    };
    const ValidationAcceptor acceptor{ValidationAcceptor::Callback(acceptorCb)};

    scenario.registerAndTrigger(registry, scenario.thrown, acceptor);

    ASSERT_EQ(messages.size(), 1u);
    EXPECT_NE(messages.front().find(scenario.diagnosticSubstr),
              std::string::npos);
    ASSERT_TRUE(context.observabilitySink->waitForCount(1));
    const auto observation = context.observabilitySink->lastObservation();
    ASSERT_TRUE(observation.has_value());
    EXPECT_EQ(observation->code, scenario.code);
    EXPECT_NE(observation->message.find(scenario.observationSubstr),
              std::string::npos);
  }
}

TEST(DefaultValidationRegistryTest,
     ValidationCheckExceptionsUseTestedNodeRange) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  registry.registerCheck<ValidationNodeA>(
      [](const ValidationNodeA &, const ValidationAcceptor &) {
        throw std::runtime_error("boom");
      });

  ParserRule<ValidationNodeA> nodeRule{"Node", "node"_kw};
  ParserRule<ValidationRoot> rootRule{
      "Root", "prefix"_kw + "-"_kw + append<&ValidationRoot::nodes>(nodeRule)};

  workspace::Document document(
      test::make_text_document("file:///validation-range.pg", "mini",
                               "prefix-node"));
  pegium::test::parse_rule(rootRule, document, SkipperBuilder().build());

  auto *root = pegium::ast_ptr_cast<ValidationRoot>(document.parseResult.value);
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(root->nodes.size(), 1u);
  auto *node = root->nodes.front();
  ASSERT_NE(node, nullptr);

  std::optional<pegium::Diagnostic> captured;
  auto acceptorCb = 
      [&captured](pegium::Diagnostic diagnostic) {
        captured = std::move(diagnostic);      };

  const ValidationAcceptor acceptor{ValidationAcceptor::Callback(acceptorCb)};

  run_checks(registry, *node, {}, acceptor);

  ASSERT_TRUE(captured.has_value());
  const auto [begin, end] = range_of(*node);
  EXPECT_EQ(captured->begin, begin);
  EXPECT_EQ(captured->end, end);
  EXPECT_NE(captured->message.find("An error occurred during validation: boom"),
            std::string::npos);
}

TEST(DefaultValidationRegistryTest,
     AstNodeCatchAllRunsAfterTypeSpecificChecksRegardlessOfRegistrationOrder) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  ValidationNodeABootstrapParser parser;
  bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                         *context.shared.astReflection);

  std::vector<std::string> order;
  // Registered FIRST, yet must still run LAST: a check targeting the
  // universal AstNode base matches every node, so it must never preempt a
  // type-specific check — regardless of registration order.
  registry.registerCheck<pegium::AstNode>(
      [&order](const pegium::AstNode &, const ValidationAcceptor &) {
        order.push_back("ast-fast");
      },
      "fast");
  registry.registerCheck<ValidationNodeA>(
      [&order](const ValidationNodeA &, const ValidationAcceptor &) {
        order.push_back("node-fast-1");
      },
      "fast");
  registry.registerCheck<ValidationNodeA>(
      [&order](const ValidationNodeA &, const ValidationAcceptor &) {
        order.push_back("node-fast-2");
      },
      "fast");

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationNodeA nodeA;
  run_checks(registry, nodeA, {}, noopAcceptor);

  EXPECT_EQ(order, (std::vector<std::string>{
                       "node-fast-1",
                       "node-fast-2",
                       "ast-fast",
                   }));
}


TEST(DefaultValidationRegistryTest, BaseTypeChecksApplyToDerivedNodes) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  ASSERT_NE(context.shared.astReflection, nullptr);
  ValidationSubtypeBootstrapParser parser;
  bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                         *context.shared.astReflection);

  std::size_t baseCalls = 0;
  registry.registerCheck<ValidationBaseNode>(
      [&baseCalls](const ValidationBaseNode &, const ValidationAcceptor &) {
        ++baseCalls;
      });

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationDerivedNode derivedNode;

  run_checks(registry, derivedNode, {}, noopAcceptor);
  run_checks(registry, derivedNode, {}, noopAcceptor);

  EXPECT_EQ(baseCalls, 2u);
  EXPECT_TRUE(context.shared.astReflection->isSubtype(
      std::type_index(typeid(ValidationDerivedNode)),
      std::type_index(typeid(ValidationBaseNode))));
}

TEST(DefaultValidationRegistryTest,
     ChecksOnAbstractBaseNotNamedByGrammarApplyToProducedNodes) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  UnnamedBaseBootstrapParser parser;
  bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                         *context.shared.astReflection);

  // The grammar produces GrammarProducedLeafNode but never names its abstract
  // C++ base, so the base is absent from reflection and its subtype edge is
  // unknown — the exact case the reflection's `isKnown` contract says needs a
  // dynamic_cast fallback.
  ASSERT_FALSE(context.shared.astReflection->isKnown(
      std::type_index(typeid(UnnamedAbstractBaseNode))));
  ASSERT_TRUE(context.shared.astReflection->isKnown(
      std::type_index(typeid(GrammarProducedLeafNode))));

  std::size_t baseCalls = 0;
  registry.registerCheck<UnnamedAbstractBaseNode>(
      [&baseCalls](const UnnamedAbstractBaseNode &,
                   const ValidationAcceptor &) { ++baseCalls; });

  auto noopAcceptorCb = [](pegium::Diagnostic) {};
  const ValidationAcceptor noopAcceptor{
      ValidationAcceptor::Callback(noopAcceptorCb)};
  GrammarProducedLeafNode leaf;

  run_checks(registry, leaf, {}, noopAcceptor);
  run_checks(registry, leaf, {}, noopAcceptor);

  EXPECT_EQ(baseCalls, 2u);
}

TEST(DefaultValidationRegistryTest,
     ChecksOnBaseAlsoRunForSiblingRuleDerivedNodes) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  SiblingRuleBootstrapParser parser;
  bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                         *context.shared.astReflection);

  // Both types are produced by real rules, so both are known to reflection.
  ASSERT_TRUE(context.shared.astReflection->isKnown(
      std::type_index(typeid(SiblingBaseNode))));
  ASSERT_TRUE(context.shared.astReflection->isKnown(
      std::type_index(typeid(SiblingDerivedNode))));
  // Root cause: the C++ `SiblingDerivedNode : SiblingBaseNode` edge must be in
  // reflection for a base check (or ast_ptr_cast<SiblingBaseNode>) to reach the
  // derived node, even though the grammar nests neither rule in the other.
  EXPECT_TRUE(context.shared.astReflection->isSubtype(
      std::type_index(typeid(SiblingDerivedNode)),
      std::type_index(typeid(SiblingBaseNode))));

  std::size_t baseCalls = 0;
  registry.registerCheck<SiblingBaseNode>(
      [&baseCalls](const SiblingBaseNode &, const ValidationAcceptor &) {
        ++baseCalls;
      });

  auto noopAcceptorCb = [](pegium::Diagnostic) {};
  const ValidationAcceptor noopAcceptor{
      ValidationAcceptor::Callback(noopAcceptorCb)};
  SiblingDerivedNode derived;

  run_checks(registry, derived, {}, noopAcceptor);

  EXPECT_EQ(baseCalls, 1u);
}

TEST(DefaultValidationRegistryTest, ExactCheckRunsOnlyForTheExactType) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  SiblingRuleBootstrapParser parser;
  bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                         *context.shared.astReflection);

  std::size_t exactCalls = 0;
  registry.registerExactCheck<SiblingBaseNode>(
      [&exactCalls](const SiblingBaseNode &, const ValidationAcceptor &) {
        ++exactCalls;
      });

  auto noopAcceptorCb = [](pegium::Diagnostic) {};
  const ValidationAcceptor noopAcceptor{
      ValidationAcceptor::Callback(noopAcceptorCb)};
  SiblingBaseNode base;
  SiblingDerivedNode derived;

  run_checks(registry, derived, {}, noopAcceptor); // subtype: must NOT fire
  EXPECT_EQ(exactCalls, 0u);
  run_checks(registry, base, {}, noopAcceptor); // exact type: fires
  EXPECT_EQ(exactCalls, 1u);
}

TEST(DefaultValidationRegistryTest,
     ExactAndSubtreeChecksMixInOneRegisterChecks) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);
  SiblingRuleBootstrapParser parser;
  bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                         *context.shared.astReflection);

  std::size_t subtreeCalls = 0;
  std::size_t exactCalls = 0;
  registry.registerChecks(
      {ValidationRegistry::makeValidationCheck<SiblingBaseNode>(
           [&subtreeCalls](const SiblingBaseNode &,
                           const ValidationAcceptor &) { ++subtreeCalls; }),
       ValidationRegistry::makeExactValidationCheck<SiblingBaseNode>(
           [&exactCalls](const SiblingBaseNode &, const ValidationAcceptor &) {
             ++exactCalls;
           })});

  auto noopAcceptorCb = [](pegium::Diagnostic) {};
  const ValidationAcceptor noopAcceptor{
      ValidationAcceptor::Callback(noopAcceptorCb)};
  SiblingBaseNode base;
  SiblingDerivedNode derived;

  run_checks(registry, base, {}, noopAcceptor);
  EXPECT_EQ(subtreeCalls, 1u);
  EXPECT_EQ(exactCalls, 1u);

  run_checks(registry, derived, {}, noopAcceptor);
  EXPECT_EQ(subtreeCalls, 2u); // subtree also fires on the subtype
  EXPECT_EQ(exactCalls, 1u);   // exact does not
}

TEST(DefaultValidationRegistryTest,
     CancellationAwareChecksReceiveTokenAndPropagateCancellation) {
  RegistryTestServices context;
  DefaultValidationRegistry registry(context.core);

  registry.registerCheck<ValidationNodeA>(
      [](const ValidationNodeA &, const ValidationAcceptor &,
         const utils::CancellationToken &cancelToken) {
        utils::throw_if_cancelled(cancelToken);
      });

  utils::CancellationTokenSource source;
  source.request_stop();

  auto noopAcceptorCb = [](pegium::Diagnostic) {};


  const ValidationAcceptor noopAcceptor{ValidationAcceptor::Callback(noopAcceptorCb)};
  ValidationNodeA node;

  EXPECT_THROW(registry.runChecks(node, noopAcceptor, {}, source.get_token()),
               utils::OperationCancelled);
}

} // namespace
} // namespace pegium::validation
