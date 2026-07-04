#include <gtest/gtest.h>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/references/References.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>

namespace pegium {
namespace {
using namespace pegium::parser;

struct DefinitionType {};

struct HighlightEntry : pegium::NamedAstNode {
  string other;
};

class HighlightParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return EntryRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<HighlightEntry> EntryRule{"Entry", assign<&HighlightEntry::name>(ID) +
                                              assign<&HighlightEntry::other>(ID)};
#pragma clang diagnostic pop
};

class TestReferences final : public references::References {
public:
  std::vector<const AstNode *> declarations;
  CstNodeView declarationNode;
  std::vector<workspace::ReferenceDescription> usages;

  std::vector<const AstNode *>
  findDeclarations(const CstNodeView &) const override {
    return declarations;
  }

  std::vector<CstNodeView>
  findDeclarationNodes(const CstNodeView &) const override {
    if (declarationNode.valid()) {
      return {declarationNode};
    }
    return {};
  }

  std::vector<workspace::ReferenceDescription>
  findReferences(const AstNode &targetNode,
                 const references::FindReferencesOptions &options) const override {
    std::vector<workspace::ReferenceDescription> results;
    if (options.includeDeclaration && declarationNode.valid()) {
      const auto &document = getDocument(targetNode);
      results.push_back(workspace::ReferenceDescription{
          .sourceDocumentId = document.id,
          .sourceOffset = declarationNode.getBegin(),
          .sourceLength = declarationNode.getEnd() - declarationNode.getBegin(),
          .local = true,
          .targetDocumentId = document.id,
          .targetSymbolId = document.makeSymbolId(targetNode),
      });
    }
    for (const auto &usage : usages) {
      if (options.documentId.has_value() &&
          usage.sourceDocumentId != *options.documentId) {
        continue;
      }
      results.push_back(usage);
    }
    return results;
  }
};

TEST(DefaultDocumentHighlightProviderTest,
     HighlightsDeclarationAndLocalReferences) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto registeredServices =
      test::make_uninstalled_services<HighlightParser>(*shared, "test", {".test"});
  pegium::installDefaultCoreServices(*registeredServices);
  pegium::installDefaultLspServices(*registeredServices);

  auto references = std::make_unique<TestReferences>();
  auto *recordingReferences = references.get();
  references->usages.push_back(workspace::ReferenceDescription{
      .sourceDocumentId = 1,
      .sourceOffset = 6,
      .sourceLength = 5,
      .targetDocumentId = 1,
      .targetSymbolId = 0,
  });
  registeredServices->references.references = std::move(references);

  shared->serviceRegistry->registerServices(std::move(registeredServices));

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("highlights.test"), "test", "value value");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->references.nameProvider, nullptr);

  auto *target = document->parseResult.value;
  ASSERT_NE(target, nullptr);
  recordingReferences->declarations.push_back(target);
  if (auto nameNode = services->references.nameProvider->getNameNode(*target);
      nameNode.has_value()) {
    recordingReferences->declarationNode = *nameNode;
  } else {
    recordingReferences->declarationNode = CstNodeView{};
  }
  ASSERT_TRUE(recordingReferences->declarationNode.valid());
  recordingReferences->usages[0].targetSymbolId = document->makeSymbolId(*target);

  ::lsp::DocumentHighlightParams params{};
  params.position.line = 0;
  params.position.character = 7;

  const auto highlights =
      services->lsp.documentHighlightProvider->getDocumentHighlight(*document,
                                                                    params);
  ASSERT_EQ(highlights.size(), 2u);
  EXPECT_EQ(highlights[0].kind, ::lsp::DocumentHighlightKind::Write);
  EXPECT_EQ(highlights[0].range.start.character, 0u);
  EXPECT_EQ(highlights[1].kind, ::lsp::DocumentHighlightKind::Read);
  EXPECT_EQ(highlights[1].range.start.character, 6u);
}

} // namespace
} // namespace pegium
