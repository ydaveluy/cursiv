// Smoke test for the public `pegium::testing` harness, exercised exactly the
// way a downstream language would: register the language on a TestWorkspace and
// drive parse / validation / completion through the public API only.
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>
#include <lsp/types.h>

#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/lsp/semantic/AbstractSemanticTokenProvider.hpp>
#include <pegium/testing/LspProbe.hpp>
#include <pegium/testing/Testing.hpp>
#include <statemachine/lsp/LspModule.hpp>

namespace {

const std::string kValidSource = "statemachine TrafficLight\n"
                                 "events\n"
                                 "    next\n"
                                 "initialState Off\n"
                                 "state Off\n"
                                 "    next => On\n"
                                 "end\n"
                                 "state On\n"
                                 "    next => Off\n"
                                 "end\n";

// A minimal semantic-token provider: every named element's name is a "variable"
// token. Lets the smoke verify the public highlight/expectSemanticToken helpers.
class NameHighlighter final : public pegium::AbstractSemanticTokenProvider {
public:
  using pegium::AbstractSemanticTokenProvider::AbstractSemanticTokenProvider;

protected:
  void
  highlightElement(const pegium::AstNode &node,
                   const pegium::SemanticTokenAcceptor &acceptor) const override {
    if (dynamic_cast<const pegium::NamedAstNode *>(&node) != nullptr) {
      highlightProperty(node, "name", "variable", acceptor);
    }
  }
};

TEST(PegiumTestingHarness, ParsesValidDocument) {
  auto ws = pegium::testing::TestWorkspace::withShared<
      statemachine::StatemachineSharedServices>();
  ws.registerLanguage(statemachine::createStatemachineLspServices(
      ws.shared<statemachine::StatemachineSharedServices>()));

  auto document = pegium::testing::parse(ws, "statemachine", kValidSource);
  ASSERT_NE(document, nullptr);
  EXPECT_TRUE(document->parseSucceeded());
}

TEST(PegiumTestingHarness, ValidationProducesDiagnosticsForGarbage) {
  auto ws = pegium::testing::TestWorkspace::withShared<
      statemachine::StatemachineSharedServices>();
  ws.registerLanguage(statemachine::createStatemachineLspServices(
      ws.shared<statemachine::StatemachineSharedServices>()));

  pegium::testing::expectValidation(
      ws, "statemachine",
      {.text = "this is not a valid statemachine",
       .check = [](const std::vector<pegium::Diagnostic> &diagnostics) {
         EXPECT_FALSE(diagnostics.empty());
       }});
}

TEST(PegiumTestingHarness, CompletionDispatchesThroughPublicApi) {
  auto ws = pegium::testing::TestWorkspace::withShared<
      statemachine::StatemachineSharedServices>();
  ws.registerLanguage(statemachine::createStatemachineLspServices(
      ws.shared<statemachine::StatemachineSharedServices>()));

  bool produced = false;
  pegium::testing::expectCompletion(
      ws, "statemachine",
      {.text = kValidSource + "<|>",
       .check = [&](const ::lsp::CompletionList &) { produced = true; }});
  EXPECT_TRUE(produced);
}

// validate() + granular expect{Error,Issue,NoIssues}.
TEST(PegiumTestingHarness, ValidationHelperAndGranularExpect) {
  auto ws = pegium::testing::TestWorkspace::withShared<
      statemachine::StatemachineSharedServices>();
  ws.registerLanguage(statemachine::createStatemachineLspServices(
      ws.shared<statemachine::StatemachineSharedServices>()));

  auto valid = pegium::testing::validate(ws, "statemachine", kValidSource);
  pegium::testing::expectNoIssues(
      valid, {.severity = pegium::DiagnosticSeverity::Error});

  std::string broken = kValidSource;
  const auto pos = broken.find("=> On");
  ASSERT_NE(pos, std::string::npos);
  broken.replace(pos, std::string("=> On").size(), "=> Ghost");
  auto invalid = pegium::testing::validate(ws, "statemachine", broken);
  pegium::testing::expectIssue(invalid);
  pegium::testing::expectError(invalid, "Could not resolve reference");
}

// The public LspProbe marker parser (replaceIndices).
TEST(PegiumTestingHarness, LspProbeMarkerParser) {
  const auto marked = pegium::testing::replaceIndices("a<|>b<| |>c");
  EXPECT_EQ(marked.output, "ab c");
  ASSERT_EQ(marked.indices.size(), 1u);
  EXPECT_EQ(marked.indices[0], 1u);
  ASSERT_EQ(marked.ranges.size(), 1u);
  EXPECT_EQ(marked.ranges[0].first, 2u);
  EXPECT_EQ(marked.ranges[0].second, 3u);
}

// highlight + expectSemanticToken.
TEST(PegiumTestingHarness, SemanticTokensHighlightAndExpect) {
  auto ws = pegium::testing::TestWorkspace::withShared<
      statemachine::StatemachineSharedServices>();
  auto services = statemachine::createStatemachineLspServices(
      ws.shared<statemachine::StatemachineSharedServices>());
  services->lsp.semanticTokenProvider =
      std::make_unique<NameHighlighter>(*services);
  ws.registerLanguage(std::move(services));

  const auto decoded = pegium::testing::highlight(ws, "statemachine",
                                                  "statemachine <|Light|>\n"
                                                  "events\n"
                                                  "    next\n"
                                                  "initialState Off\n"
                                                  "state Off\n"
                                                  "    next => Off\n"
                                                  "end\n");
  pegium::testing::expectSemanticToken(decoded, {.tokenType = "variable"});
}

// Hardening: a find-references source with no `<|>` cursor marker drives no
// assertions, so the helper must fail loudly instead of silently passing.
TEST(PegiumTestingHarness, FindReferencesFailsWhenSourceHasNoCursorMarker) {
  auto ws = pegium::testing::TestWorkspace::withShared<
      statemachine::StatemachineSharedServices>();
  ws.registerLanguage(statemachine::createStatemachineLspServices(
      ws.shared<statemachine::StatemachineSharedServices>()));

  EXPECT_NONFATAL_FAILURE(
      pegium::testing::expectFindReferences(ws, "statemachine",
                                            {.text = kValidSource}),
      "cursor marker");
}

// Hardening: an explicit range selector beyond the marked-range count must be
// dropped, not handed back to be used as an out-of-bounds vector index.
TEST(PegiumTestingHarness, ResolveRangeIndicesDropsOutOfRangeSelectors) {
  const auto resolved =
      pegium::testing::resolveRangeIndices(std::vector<std::size_t>{0, 5}, 2);
  EXPECT_EQ(resolved, (std::vector<std::size_t>{0}));

  const auto single = pegium::testing::resolveRangeIndices(std::size_t{5}, 2);
  EXPECT_TRUE(single.empty());
}

} // namespace
