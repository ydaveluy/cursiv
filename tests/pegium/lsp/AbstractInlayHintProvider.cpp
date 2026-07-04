#include <gtest/gtest.h>

#include <string>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/lsp/semantic/AbstractInlayHintProvider.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>

namespace pegium {
namespace {

using namespace pegium::parser;

struct HintEntry : AstNode {
  string name;
};

struct HintModel : AstNode {
  vector<pointer<HintEntry>> entries;
};

class HintParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return ModelRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<HintEntry> EntryRule{"Entry", "entry"_kw + assign<&HintEntry::name>(ID)};
  Rule<HintModel> ModelRule{"Model", some(append<&HintModel::entries>(EntryRule))};
#pragma clang diagnostic pop
};

const pegium::Services *lookup_services(const pegium::SharedServices &shared,
                                        std::string_view languageId) {
  for (const auto *coreServices : shared.serviceRegistry->all()) {
    if (coreServices != nullptr &&
        coreServices->languageMetaData.languageId == languageId) {
      const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
      if (services != nullptr) {
        return services;
      }
    }
  }
  return nullptr;
}

class TestInlayHintProvider final : public AbstractInlayHintProvider {
public:
  using AbstractInlayHintProvider::AbstractInlayHintProvider;

  mutable std::vector<std::string> visitedNames;

protected:
  void computeInlayHint(const AstNode &astNode,
                        const InlayHintAcceptor &acceptor) const override {
    const auto *entry = dynamic_cast<const HintEntry *>(&astNode);
    if (entry == nullptr) {
      return;
    }

    visitedNames.push_back(entry->name);

    ::lsp::InlayHint hint{};
    hint.position = getDocument(astNode)
                        .textDocument()
                        .positionAt(astNode.getCstNode().getBegin());
    hint.label = entry->name;
    acceptor(std::move(hint));
  }
};

TEST(AbstractInlayHintProviderTest, VisitsOnlyAstNodesInRequestedRange) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<HintParser>(*shared, "hint", {".hint"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("hint.hint"), "hint",
      "entry Alpha\n"
      "entry Beta");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "hint");
  ASSERT_NE(services, nullptr);

  TestInlayHintProvider provider(*services);

  ::lsp::InlayHintParams params{};
  params.range.start = document->textDocument().positionAt(
      static_cast<TextOffset>(document->textDocument().getText().find("entry Beta")));
  params.range.end = document->textDocument().positionAt(
      static_cast<TextOffset>(document->textDocument().getText().size()));

  const auto hints =
      provider.getInlayHints(*document, params, utils::default_cancel_token);
  ASSERT_EQ(hints.size(), 1u);
  EXPECT_EQ(provider.visitedNames, std::vector<std::string>{"Beta"});
  ASSERT_TRUE(std::holds_alternative<::lsp::String>(hints[0].label));
  EXPECT_EQ(std::get<::lsp::String>(hints[0].label), "Beta");
}

} // namespace
} // namespace pegium
