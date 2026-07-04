#include <gtest/gtest.h>

#include <pegium/lsp/navigation/AbstractGoToImplementationProvider.hpp>

#include "AbstractNavigationProviderTestUtils.hpp"

namespace pegium {
namespace {

using namespace test;

class TestImplementationProvider final
    : public AbstractGoToImplementationProvider {
public:
  using AbstractGoToImplementationProvider::AbstractGoToImplementationProvider;

  mutable std::string seenName;

protected:
  std::optional<std::vector<::lsp::LocationLink>>
  collectGoToImplementationLocationLinks(
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

TEST(AbstractGoToImplementationProviderTest,
     DelegatesResolvedDeclarationNode) {
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
      *shared, test::make_file_uri("implementation.nav"), "nav",
      "entry Alpha\n"
      "use Alpha");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestImplementationProvider provider(*services);

  ::lsp::ImplementationParams params{};
  params.position =
      document->textDocument().positionAt(use_name_offset(*document) + 1);

  const auto links =
      provider.getImplementation(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(links.has_value());
  ASSERT_EQ(links->size(), 1u);
  EXPECT_EQ(provider.seenName, "Alpha");
  EXPECT_EQ((*links)[0].targetUri.toString(), document->uri);
}

TEST(AbstractGoToImplementationProviderTest,
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

  // `link Alpha` is a multi-reference resolving to BOTH `entry Alpha`
  // declarations, so the provider must return a link for each.
  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("implementation-multi.nav"), "nav",
      "entry Alpha\n"
      "entry Alpha\n"
      "link Alpha");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestImplementationProvider provider(*services);

  ::lsp::ImplementationParams params{};
  params.position =
      document->textDocument().positionAt(use_name_offset(*document) + 1);

  const auto links =
      provider.getImplementation(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(links.has_value());
  EXPECT_EQ(links->size(), 2u);
}

} // namespace
} // namespace pegium
