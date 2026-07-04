#include <gtest/gtest.h>

#include <pegium/lsp/navigation/AbstractTypeDefinitionProvider.hpp>

#include "AbstractNavigationProviderTestUtils.hpp"

namespace pegium {
namespace {

using namespace test;

class TestTypeDefinitionProvider final : public AbstractTypeDefinitionProvider {
public:
  using AbstractTypeDefinitionProvider::AbstractTypeDefinitionProvider;

  mutable std::string seenName;

protected:
  std::optional<std::vector<::lsp::LocationLink>>
  collectGoToTypeLocationLinks(
      const AstNode &element,
      const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    const auto *entry = dynamic_cast<const NavigationEntry *>(&element);
    if (entry == nullptr) {
      return std::nullopt;
    }
    seenName = entry->name;
    return std::vector<::lsp::LocationLink>{link_to_element(element)};
  }
};

TEST(AbstractTypeDefinitionProviderTest, DelegatesResolvedDeclarationNode) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<NavigationParser>(*shared, "nav", {".nav"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("type-definition.nav"), "nav",
      "entry Alpha\n"
      "use Alpha");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestTypeDefinitionProvider provider(*services);

  ::lsp::TypeDefinitionParams params{};
  params.position =
      document->textDocument().positionAt(use_name_offset(*document) + 1);

  const auto links =
      provider.getTypeDefinition(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(links.has_value());
  ASSERT_EQ(links->size(), 1u);
  EXPECT_EQ(provider.seenName, "Alpha");
  EXPECT_EQ((*links)[0].targetUri.toString(), document->uri);
}

TEST(AbstractTypeDefinitionProviderTest,
     CollectsLinksForEveryResolvedDeclaration) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices =
      test::make_uninstalled_services<NavigationParser>(*shared, "nav", {".nav"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  // `link Alpha` resolves (multi-reference) to BOTH `entry Alpha` declarations.
  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("type-definition-multi.nav"), "nav",
      "entry Alpha\n"
      "entry Alpha\n"
      "link Alpha");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestTypeDefinitionProvider provider(*services);

  ::lsp::TypeDefinitionParams params{};
  params.position =
      document->textDocument().positionAt(use_name_offset(*document) + 1);

  const auto links =
      provider.getTypeDefinition(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(links.has_value());
  EXPECT_EQ(links->size(), 2u);
}

} // namespace
} // namespace pegium
