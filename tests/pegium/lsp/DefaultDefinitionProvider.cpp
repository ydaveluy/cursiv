#include <gtest/gtest.h>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/lsp/services/Services.hpp>

namespace pegium {
namespace {
using namespace pegium::parser;

struct Package : pegium::NamedAstNode {};

struct Use : AstNode {
  reference<Package> target;
};

struct DefinitionModel : AstNode {
  vector<pointer<Package>> packages;
  vector<pointer<Use>> uses;
};

class DefinitionParser final : public PegiumParser {
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
  Rule<std::string> QualifiedName{"QualifiedName", some(ID, "."_kw)};

  Rule<Package> PackageRule{"Package",
                            "package"_kw + assign<&Package::name>(QualifiedName)};
  Rule<Use> UseRule{"Use", "use"_kw + assign<&Use::target>(QualifiedName)};
  Rule<DefinitionModel> ModelRule{
      "Model", some(append<&DefinitionModel::packages>(PackageRule) |
                    append<&DefinitionModel::uses>(UseRule))};
#pragma clang diagnostic pop
};

std::shared_ptr<workspace::Document>
build_definition_document(pegium::SharedServices &shared, std::string text) {
  {
    auto registeredServices =
        test::make_uninstalled_services<DefinitionParser>(shared, "def",
                                                          {".def"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared.serviceRegistry->registerServices(std::move(registeredServices));
  }
  return test::open_and_build_document(shared, test::make_file_uri("def.def"),
                                       "def", std::move(text));
}

TEST(DefaultDefinitionProviderTest,
     LinkSpansFullDeclarationNameAndQualifiedOrigin) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);

  // `package a.b` declares a qualified name; `use a.b` references it through a
  // datatype rule, so the origin must widen to the whole qualified name.
  auto document = build_definition_document(*shared,
                                            "package a.b\n"
                                            "use a.b\n");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.definitionProvider, nullptr);

  const auto &text = document->textDocument().getText();
  const auto useNameOffset = static_cast<TextOffset>(text.find("a.b", text.find("use")));

  ::lsp::DefinitionParams params{};
  params.position = document->textDocument().positionAt(useNameOffset);

  const auto links = services->lsp.definitionProvider->getDefinition(
      *document, params, utils::default_cancel_token);
  ASSERT_TRUE(links.has_value());
  ASSERT_EQ(links->size(), 1u);
  const auto &link = links->front();

  const auto expectRange = [&](const ::lsp::Range &actual, std::string_view needle,
                               std::size_t from) {
    const auto begin = static_cast<TextOffset>(text.find(needle, from));
    const auto end = static_cast<TextOffset>(begin + needle.size());
    const auto expectedStart = document->textDocument().positionAt(begin);
    const auto expectedEnd = document->textDocument().positionAt(end);
    EXPECT_EQ(actual.start.line, expectedStart.line);
    EXPECT_EQ(actual.start.character, expectedStart.character);
    EXPECT_EQ(actual.end.line, expectedEnd.line);
    EXPECT_EQ(actual.end.character, expectedEnd.character);
  };

  // targetRange: the whole `package a.b` declaration (peek preview).
  expectRange(link.targetRange, "package a.b", 0);
  // targetSelectionRange: the declaration name `a.b`.
  expectRange(link.targetSelectionRange, "a.b", 0);
  // originSelectionRange: the whole qualified name `a.b` in the use site.
  ASSERT_TRUE(link.originSelectionRange.has_value());
  expectRange(*link.originSelectionRange, "a.b", text.find("use"));
}

} // namespace
} // namespace pegium
