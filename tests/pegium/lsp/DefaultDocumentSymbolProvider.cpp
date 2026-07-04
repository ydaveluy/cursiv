#include <gtest/gtest.h>

#include <pegium/lsp/LspExpectTestSupport.hpp>
#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/core/workspace/AstDescriptions.hpp>

namespace pegium {
namespace {
using namespace pegium::parser;

struct SymbolEntry : pegium::NamedAstNode {};

struct SymbolModel : AstNode {
  vector<pointer<SymbolEntry>> entries;
};

class SymbolParser final : public PegiumParser {
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
  Rule<SymbolEntry> EntryRule{"Entry",
                              "entry"_kw + assign<&SymbolEntry::name>(ID)};
  Rule<SymbolModel> ModelRule{
      "Model", some(append<&SymbolModel::entries>(EntryRule))};
#pragma clang diagnostic pop
};

TEST(DefaultDocumentSymbolProviderTest, ReturnsEmptyWithoutAstOrNameProvider) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  workspace::Document document(
      test::make_text_document(test::make_file_uri("symbols.test"), "test",
                               "alpha beta"));
  document.id = 1;

  const auto *coreServices = &shared->serviceRegistry->getServices(document.uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.documentSymbolProvider, nullptr);

  const auto symbols = services->lsp.documentSymbolProvider->getSymbols(
      document, ::lsp::DocumentSymbolParams{}, utils::default_cancel_token);
  EXPECT_TRUE(symbols.empty());
}

TEST(DefaultDocumentSymbolProviderTest, UsesAstNamesWhenAvailable) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<SymbolParser>(*shared, "symbols", {".symbols"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = pegium::test::expectSymbols(
      *shared, "symbols",
      pegium::test::ExpectedSymbols{
          .text = "entry Alpha\nentry Beta\n",
          .expectedSymbols = {"Alpha", "Beta"},
      });
  ASSERT_NE(document, nullptr);
}

TEST(DefaultDocumentSymbolProviderTest, UsesFullNodeRangeAndNameSelectionRange) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices =
        test::make_uninstalled_services<SymbolParser>(*shared, "symbols", {".symbols"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("document-symbols.symbols"), "symbols",
      "entry Alpha\n");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  const auto symbols = services->lsp.documentSymbolProvider->getSymbols(
      *document, ::lsp::DocumentSymbolParams{}, utils::default_cancel_token);
  ASSERT_EQ(symbols.size(), 1u);
  EXPECT_EQ(symbols[0].name, "Alpha");
  EXPECT_EQ(symbols[0].range.start.character, 0u);
  EXPECT_EQ(symbols[0].range.end.character, 11u);
  EXPECT_EQ(symbols[0].selectionRange.start.character, 6u);
  EXPECT_EQ(symbols[0].selectionRange.end.character, 11u);
}

} // namespace
} // namespace pegium
