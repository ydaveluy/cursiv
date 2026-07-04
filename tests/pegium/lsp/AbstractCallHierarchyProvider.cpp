#include <gtest/gtest.h>

#include <pegium/lsp/hierarchy/AbstractCallHierarchyProvider.hpp>

#include "AbstractNavigationProviderTestUtils.hpp"

namespace pegium {
namespace {

using namespace test;

class TestCallHierarchyProvider final : public AbstractCallHierarchyProvider {
public:
  using AbstractCallHierarchyProvider::AbstractCallHierarchyProvider;

  mutable std::string incomingName;
  mutable std::string outgoingName;

protected:
  std::vector<::lsp::CallHierarchyIncomingCall>
  getIncomingCalls(const AstNode &node,
                   const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    incomingName = static_cast<const NavigationEntry &>(node).name;

    ::lsp::CallHierarchyIncomingCall call{};
    call.from = makeAuxItem("incoming");
    call.fromRanges.push_back(call.from.selectionRange);
    return {std::move(call)};
  }

  std::vector<::lsp::CallHierarchyOutgoingCall>
  getOutgoingCalls(const AstNode &node,
                   const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    outgoingName = static_cast<const NavigationEntry &>(node).name;

    ::lsp::CallHierarchyOutgoingCall call{};
    call.to = makeAuxItem("outgoing");
    call.fromRanges.push_back(call.to.selectionRange);
    return {std::move(call)};
  }

  void customizeCallHierarchyItem(
      const AstNode &, ::lsp::CallHierarchyItem &item) const override {
    item.detail = "custom call";
    item.selectionRange.start.line = item.range.start.line;
    item.selectionRange.start.character = 0;
    item.selectionRange.end = item.selectionRange.start;
  }

private:
  ::lsp::CallHierarchyItem makeAuxItem(std::string name) const {
    ::lsp::CallHierarchyItem item{};
    item.name = std::move(name);
    item.kind = ::lsp::SymbolKind::Method;
    item.uri = ::lsp::DocumentUri(::lsp::Uri::parse("file:///tmp/dummy"));
    return item;
  }
};

TEST(AbstractCallHierarchyProviderTest,
     CreatesDefaultItemAndDelegatesIncomingOutgoing) {
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
      *shared, test::make_file_uri("call-hierarchy.nav"), "nav",
      "entry Alpha\n"
      "use Alpha");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestCallHierarchyProvider provider(*services);

  ::lsp::CallHierarchyPrepareParams prepareParams{};
  prepareParams.position =
      document->textDocument().positionAt(use_name_offset(*document) + 1);

  const auto items = provider.prepareCallHierarchy(
      *document, prepareParams, utils::default_cancel_token);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].name, "Alpha");
  EXPECT_EQ(items[0].kind, ::lsp::SymbolKind::Method);
  EXPECT_EQ(items[0].uri.toString(), document->uri);
  EXPECT_EQ(items[0].detail.value_or(""), "custom call");
  EXPECT_EQ(items[0].range.start.character, 0u);
  EXPECT_EQ(items[0].selectionRange.start.character, 0u);

  ::lsp::CallHierarchyIncomingCallsParams incomingParams{};
  incomingParams.item = items[0];
  const auto incoming =
      provider.incomingCalls(incomingParams, utils::default_cancel_token);
  ASSERT_EQ(incoming.size(), 1u);
  EXPECT_EQ(provider.incomingName, "Alpha");

  ::lsp::CallHierarchyOutgoingCallsParams outgoingParams{};
  outgoingParams.item = items[0];
  const auto outgoing =
      provider.outgoingCalls(outgoingParams, utils::default_cancel_token);
  ASSERT_EQ(outgoing.size(), 1u);
  EXPECT_EQ(provider.outgoingName, "Alpha");
}

TEST(AbstractCallHierarchyProviderTest, PrepareEmitsItemPerResolvedDeclaration) {
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
  // declarations, so prepare must emit one item per declaration.
  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("call-hierarchy-multi.nav"), "nav",
      "entry Alpha\n"
      "entry Alpha\n"
      "link Alpha");
  ASSERT_NE(document, nullptr);

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestCallHierarchyProvider provider(*services);

  ::lsp::CallHierarchyPrepareParams prepareParams{};
  prepareParams.position =
      document->textDocument().positionAt(use_name_offset(*document) + 1);

  const auto items = provider.prepareCallHierarchy(
      *document, prepareParams, utils::default_cancel_token);
  EXPECT_EQ(items.size(), 2u);
}

TEST(AbstractCallHierarchyProviderTest, SkipsEdgesWhenItemDocumentIsNotLoadable) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  // Guarantee the unknown document cannot be materialized from disk.
  shared->workspace.fileSystemProvider =
      std::make_shared<workspace::EmptyFileSystemProvider>();
  {
    auto registeredServices =
      test::make_uninstalled_services<NavigationParser>(*shared, "nav", {".nav"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  const auto *services = lookup_services(*shared, "nav");
  ASSERT_NE(services, nullptr);

  TestCallHierarchyProvider provider(*services);

  // The item points at a document that is neither loaded nor loadable. The edge
  // flow must skip it gracefully instead of asserting on the missing document.
  ::lsp::CallHierarchyItem item{};
  item.name = "Ghost";
  item.kind = ::lsp::SymbolKind::Method;
  item.uri =
      ::lsp::DocumentUri(::lsp::Uri::parse(test::make_file_uri("missing.nav")));

  ::lsp::CallHierarchyIncomingCallsParams incomingParams{};
  incomingParams.item = item;
  EXPECT_TRUE(
      provider.incomingCalls(incomingParams, utils::default_cancel_token).empty());

  ::lsp::CallHierarchyOutgoingCallsParams outgoingParams{};
  outgoingParams.item = item;
  EXPECT_TRUE(
      provider.outgoingCalls(outgoingParams, utils::default_cancel_token).empty());
}

} // namespace
} // namespace pegium
