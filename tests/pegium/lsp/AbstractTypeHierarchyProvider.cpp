#include <gtest/gtest.h>

#include <pegium/lsp/hierarchy/AbstractTypeHierarchyProvider.hpp>

#include "AbstractNavigationProviderTestUtils.hpp"

namespace pegium {
namespace {

using namespace test;

class TestTypeHierarchyProvider final : public AbstractTypeHierarchyProvider {
public:
  using AbstractTypeHierarchyProvider::AbstractTypeHierarchyProvider;

  mutable std::string supertypeName;
  mutable std::string subtypeName;

protected:
  std::vector<::lsp::TypeHierarchyItem>
  getSupertypes(const AstNode &node,
                const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    supertypeName = static_cast<const NavigationEntry &>(node).name;
    return {makeAuxItem("super")};
  }

  std::vector<::lsp::TypeHierarchyItem>
  getSubtypes(const AstNode &node,
              const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    subtypeName = static_cast<const NavigationEntry &>(node).name;
    return {makeAuxItem("sub")};
  }

  void customizeTypeHierarchyItem(
      const AstNode &, ::lsp::TypeHierarchyItem &item) const override {
    item.detail = "custom type";
    item.selectionRange.start.line = item.range.start.line;
    item.selectionRange.start.character = 0;
    item.selectionRange.end = item.selectionRange.start;
  }

private:
  ::lsp::TypeHierarchyItem makeAuxItem(std::string name) const {
    ::lsp::TypeHierarchyItem item{};
    item.name = std::move(name);
    item.kind = ::lsp::SymbolKind::Class;
    item.uri = ::lsp::DocumentUri(::lsp::Uri::parse("file:///tmp/dummy"));
    return item;
  }
};

TEST(AbstractTypeHierarchyProviderTest,
     CreatesDefaultItemAndDelegatesSuperSubTypes) {
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
      *shared, test::make_file_uri("type-hierarchy.nav"), "nav",
      "entry Alpha\n"
      "use Alpha");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestTypeHierarchyProvider provider(*services);

  ::lsp::TypeHierarchyPrepareParams prepareParams{};
  prepareParams.position =
      document->textDocument().positionAt(use_name_offset(*document) + 1);

  const auto items = provider.prepareTypeHierarchy(
      *document, prepareParams, utils::default_cancel_token);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].name, "Alpha");
  EXPECT_EQ(items[0].kind, ::lsp::SymbolKind::Class);
  EXPECT_EQ(items[0].uri.toString(), document->uri);
  EXPECT_EQ(items[0].detail.value_or(""), "custom type");
  EXPECT_EQ(items[0].range.start.character, 0u);
  EXPECT_EQ(items[0].selectionRange.start.character, 0u);

  ::lsp::TypeHierarchySupertypesParams supertypesParams{};
  supertypesParams.item = items[0];
  const auto supertypes =
      provider.supertypes(supertypesParams, utils::default_cancel_token);
  ASSERT_EQ(supertypes.size(), 1u);
  EXPECT_EQ(provider.supertypeName, "Alpha");

  ::lsp::TypeHierarchySubtypesParams subtypesParams{};
  subtypesParams.item = items[0];
  const auto subtypes =
      provider.subtypes(subtypesParams, utils::default_cancel_token);
  ASSERT_EQ(subtypes.size(), 1u);
  EXPECT_EQ(provider.subtypeName, "Alpha");
}

} // namespace
} // namespace pegium
