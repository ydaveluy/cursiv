#include <gtest/gtest.h>

#include <memory>

#include <pegium/core/TestCstBuilderHarness.hpp>
#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/lsp/services/Services.hpp>

namespace pegium {
namespace {
using namespace pegium::parser;

TEST(DefaultFoldingRangeProviderTest, ReturnsVisibleRangesSpanningMultipleLines) {
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
      test::make_text_document(test::make_file_uri("folding.test"), "test",
                               "line1\nline2\nline3"));

  auto root = std::make_unique<RootCstNode>(text::TextSnapshot::copy(document.textDocument().getText()));
  root->attachDocument(document);
  CstBuilder builder(*root);
  static constexpr pegium::parser::Literal<std::array{'r', 'e', 'g', 'i', 'o',
                                                      'n'}>
      region{};
  builder.leaf(0,
               static_cast<pegium::TextOffset>(document.textDocument().getText().size()),
               std::addressof(region));
  document.parseResult.cst = std::move(root);

  const auto *coreServices = &shared->serviceRegistry->getServices(document.uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  const auto ranges = services->lsp.foldingRangeProvider->getFoldingRanges(
      document, ::lsp::FoldingRangeParams{});
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_EQ(ranges[0].startLine, 0u);
  EXPECT_GT(ranges[0].endLine, ranges[0].startLine);
}

TEST(DefaultFoldingRangeProviderTest, SkipsHiddenNodes) {
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

  workspace::Document document(test::make_text_document(
      test::make_file_uri("hidden-folding.test"), "test", "line1\nline2"));

  auto root = std::make_unique<RootCstNode>(text::TextSnapshot::copy(document.textDocument().getText()));
  root->attachDocument(document);
  CstBuilder builder(*root);
  static constexpr pegium::parser::Literal<std::array{'h', 'i', 'd', 'd', 'e',
                                                      'n'}>
      hidden{};
  builder.leaf(0,
               static_cast<pegium::TextOffset>(document.textDocument().getText().size()),
               std::addressof(hidden), true);
  document.parseResult.cst = std::move(root);

  const auto *coreServices = &shared->serviceRegistry->getServices(document.uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  EXPECT_TRUE(services->lsp.foldingRangeProvider
                  ->getFoldingRanges(document, ::lsp::FoldingRangeParams{})
                  .empty());
}

struct FoldEntry : AstNode {
  string name;
};
struct FoldBlock : AstNode {
  vector<pointer<FoldEntry>> entries;
};
struct FoldModel : AstNode {
  vector<pointer<FoldBlock>> blocks;
};

class FoldingParser final : public PegiumParser {
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
  Skipper skipper = SkipperBuilder().ignore(WS).hide(ML_COMMENT).build();

  Terminal<> ML_COMMENT{"ML_COMMENT", "/*"_kw <=> "*/"_kw};
  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<FoldEntry> EntryRule{"Entry", "entry"_kw + assign<&FoldEntry::name>(ID)};
  Rule<FoldBlock> BlockRule{
      "Block", "{"_kw + some(append<&FoldBlock::entries>(EntryRule)) + "}"_kw};
  Rule<FoldModel> ModelRule{"Model",
                            some(append<&FoldModel::blocks>(BlockRule))};
#pragma clang diagnostic pop
};

TEST(DefaultFoldingRangeProviderTest, FoldsCommentsAndTrimsTheClosingLine) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices =
        test::make_uninstalled_services<FoldingParser>(*shared, "fold",
                                                       {".fold"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("folding.fold"), "fold",
      "/*\n"        // line 0
      " multi\n"    // line 1
      " line\n"     // line 2
      "*/\n"        // line 3
      "{\n"         // line 4
      "  entry A\n" // line 5
      "  entry B\n" // line 6
      "}\n");       // line 7
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  const auto ranges = services->lsp.foldingRangeProvider->getFoldingRanges(
      *document, ::lsp::FoldingRangeParams{});

  const auto find =
      [&](::lsp::FoldingRangeKind kind,
          std::uint32_t startLine) -> const ::lsp::FoldingRange * {
    for (const auto &range : ranges) {
      if (range.kind.has_value() && *range.kind == kind &&
          range.startLine == startLine) {
        return &range;
      }
    }
    return nullptr;
  };

  // The multi-line comment folds, with the closing `*/` line kept visible.
  const auto *comment = find(::lsp::FoldingRangeKind::Comment, 0u);
  ASSERT_NE(comment, nullptr);
  EXPECT_EQ(comment->endLine, 2u);

  // The brace block folds, with the closing `}` line kept visible.
  const auto *block = find(::lsp::FoldingRangeKind::Region, 4u);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(block->endLine, 6u);
}

} // namespace
} // namespace pegium
