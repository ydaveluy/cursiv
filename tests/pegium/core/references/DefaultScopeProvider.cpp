#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <pegium/core/CoreTestSupport.hpp>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/FeatureValue.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/references/DefaultScopeProvider.hpp>
#include <pegium/core/references/ScopeProvider.hpp>
#include <pegium/core/syntax-tree/CstBuilder.hpp>
#include <pegium/core/syntax-tree/RootCstNode.hpp>

namespace pegium::references {
namespace {

using namespace pegium::parser;

struct TargetNode final : AstNode {};
struct BaseTargetNode : AstNode {};
struct DerivedTargetNode final : BaseTargetNode {};

struct RefHolder final : AstNode {
  Reference<TargetNode> ref;
};

struct BaseRefHolder final : AstNode {
  Reference<BaseTargetNode> ref;
};

struct ScopeBlock final : AstNode {
  vector<pointer<ScopeBlock>> blocks;
  vector<pointer<RefHolder>> refs;
};

struct ScopeRoot final : AstNode {
  vector<pointer<ScopeBlock>> blocks;
};

class ScopeSubtypeBootstrapParser final : public PegiumParser {
protected:
  const grammar::ParserRule &getEntryRule() const noexcept override {
    return BaseRule;
  }

  Rule<DerivedTargetNode> DerivedRule{"Derived", "derived"_kw};
  Rule<BaseTargetNode> BaseRule{"Base", DerivedRule};
};

template <typename OwnerType, typename TargetType>
struct TestReferenceAssignment final : grammar::Assignment {
  explicit TestReferenceAssignment(std::string_view feature) noexcept
      : feature(feature) {}

  [[nodiscard]] constexpr bool isNullable() const noexcept override {
    return false;
  }
  void execute(AstNode *, const CstNodeView &,
               const parser::ValueBuildContext &) const override {}
  [[nodiscard]] grammar::FeatureValue
  getValue(const AstNode *) const override {
    return {};
  }
  [[nodiscard]] const grammar::AbstractElement *
  getElement() const noexcept override {
    return nullptr;
  }
  [[nodiscard]] std::string_view getFeature() const noexcept override {
    return feature;
  }
  [[nodiscard]] bool isReference() const noexcept override { return true; }
  [[nodiscard]] bool isMultiReference() const noexcept override {
    return false;
  }
  [[nodiscard]] std::type_index getType() const noexcept override {
    return std::type_index(typeid(TargetType));
  }

  std::string_view feature;
};

struct DummyLiteral final : grammar::Literal {
  [[nodiscard]] bool isNullable() const noexcept override { return false; }

  [[nodiscard]] std::string_view getValue() const noexcept override {
    return "x";
  }

  [[nodiscard]] bool isCaseSensitive() const noexcept override { return true; }
};

class TestIndexManager final : public workspace::IndexManager {
public:
  explicit TestIndexManager(const AstReflection *reflection = nullptr)
      : _reflection(reflection) {}

  void updateContent(workspace::Document &,
                     utils::CancellationToken) override {}

  void updateReferences(workspace::Document &,
                        utils::CancellationToken) override {}

  bool removeContent(workspace::DocumentId documentId) override {
    return _exportsByDocument.erase(documentId) > 0;
  }

  bool removeReferences(workspace::DocumentId documentId) override {
    return _referencesByDocument.erase(documentId) > 0;
  }

  bool remove(workspace::DocumentId documentId) override {
    const bool removedContent = removeContent(documentId);
    const bool removedReferences = removeReferences(documentId);
    return removedContent || removedReferences;
  }

  std::vector<workspace::AstNodeDescription>
  allElements(std::optional<std::type_index> type = std::nullopt,
              std::span<const workspace::DocumentId> documentIds = {}) const override {
    std::vector<workspace::AstNodeDescription> result;

    auto append = [&](workspace::DocumentId documentId) {
      const auto it = _exportsByDocument.find(documentId);
      if (it == _exportsByDocument.end()) {
        return;
      }
      for (const auto &description : it->second) {
        if (!type.has_value() || matchesType(description.type, *type)) {
          result.push_back(description);
        }
      }
    };

    if (documentIds.empty()) {
      for (const auto &[documentId, exports] : _exportsByDocument) {
        (void)exports;
        append(documentId);
      }
    } else {
      for (const auto documentId : documentIds) {
        append(documentId);
      }
    }

    return result;
  }

  std::optional<workspace::AstNodeDescription>
  findByName(std::string_view name,
             std::optional<std::type_index> type = std::nullopt) const override {
    for (const auto &description : allElements(type)) {
      if (description.name == name) {
        return description;
      }
    }
    return std::nullopt;
  }

  std::vector<workspace::ReferenceDescription>
  findAllReferences(const workspace::NodeKey &) const override {
    return {};
  }

  bool isAffected(
      const workspace::Document &,
      const std::unordered_set<workspace::DocumentId> &) const override {
    return false;
  }

  void setExports(
      workspace::DocumentId documentId,
      std::vector<workspace::AstNodeDescription> exports) {
    for (auto &entry : exports) {
      entry.documentId = documentId;
    }
    _exportsByDocument.insert_or_assign(documentId, std::move(exports));
  }

private:
  bool matchesType(std::type_index actual, std::type_index expected) const {
    if (actual == expected) {
      return true;
    }
    return _reflection != nullptr && _reflection->isSubtype(actual, expected);
  }

  const AstReflection *_reflection;
  std::unordered_map<workspace::DocumentId,
                     std::vector<workspace::AstNodeDescription>>
      _exportsByDocument;
  std::unordered_map<workspace::DocumentId,
                     std::vector<workspace::ReferenceDescription>>
      _referencesByDocument;
};

class OverridingGlobalScopeProvider final : public DefaultScopeProvider {
public:
  OverridingGlobalScopeProvider(
      const pegium::CoreServices &services,
      std::vector<workspace::AstNodeDescription> entries)
      : DefaultScopeProvider(services),
        _entries(build_entries(std::move(entries))) {}

protected:
  std::shared_ptr<const CompiledGlobalEntries>
  getGlobalEntries(std::type_index) const override {
    return _entries;
  }

private:
  static std::shared_ptr<const CompiledGlobalEntries>
  build_entries(std::vector<workspace::AstNodeDescription> entries) {
    auto compiled = std::make_shared<CompiledGlobalEntries>();
    compiled->elements = std::move(entries);
    for (const auto &entry : compiled->elements) {
      compiled->entriesByName[entry.name].add(entry);
    }
    return compiled;
  }

  std::shared_ptr<const CompiledGlobalEntries> _entries;
};

struct ScopedDocumentFixture {
  std::shared_ptr<workspace::Document> document;
  ScopeRoot *root = nullptr;
  ScopeBlock *outer = nullptr;
  ScopeBlock *inner = nullptr;
  RefHolder *holder = nullptr;
};

template <typename Holder>
struct AttachedReferenceHolderFixture {
  std::shared_ptr<workspace::Document> document;
  Holder *holder = nullptr;
};

ScopedDocumentFixture make_scoped_document(pegium::SharedCoreServices &shared,
                                           const references::Linker &linker,
                                           std::string uri,
                                           std::string refText = "target") {
  auto document = std::make_shared<workspace::Document>(
      test::make_text_document(std::move(uri), "test", "x"));
  shared.workspace.documents->addDocument(document);

  auto cst = std::make_unique<RootCstNode>(text::TextSnapshot::copy(document->textDocument().getText()));
  cst->attachDocument(*document);
  static const DummyLiteral literal;
  CstBuilder builder(*cst);
  builder.leaf(0, 1, &literal);

  document->parseResult.astArena = std::make_unique<pegium::AstArena>(*cst);


  auto &arena = *document->parseResult.astArena;
  arena.attachDocument(*document);
  auto *root = arena.create<ScopeRoot>();
  root->setCstNode(cst->get(0));

  auto *outer = arena.create<ScopeBlock>();
  outer->setCstNode(cst->get(0));

  auto *inner = arena.create<ScopeBlock>();
  inner->setCstNode(cst->get(0));

  static const TestReferenceAssignment<RefHolder, TargetNode> assignment("ref");
  auto *holder = arena.create<RefHolder>();
  holder->setCstNode(cst->get(0));
  holder->ref.initialize(*holder, std::move(refText), CstNodeView{},
                         assignment, linker);

  inner->refs.push_back(holder);
  holder->setContainer(*inner);

  outer->blocks.push_back(inner);
  inner->setContainer(*outer);

  root->blocks.push_back(outer);
  outer->setContainer(*root);

  document->parseResult.value = root;
  document->parseResult.cst = std::move(cst);
  document->parseResult.references.push_back(ReferenceHandle::direct(&holder->ref));

  return {.document = std::move(document),
          .root = root,
          .outer = outer,
          .inner = inner,
          .holder = holder};
}

template <typename Holder, typename TargetType>
AttachedReferenceHolderFixture<Holder>
make_attached_reference_holder(pegium::SharedCoreServices &shared,
                               const references::Linker &linker, std::string uri,
                               std::string refText = "target") {
  auto document = std::make_shared<workspace::Document>(
      test::make_text_document(std::move(uri), "test", refText));
  shared.workspace.documents->addDocument(document);

  auto cst = std::make_unique<RootCstNode>(text::TextSnapshot::copy(document->textDocument().getText()));
  cst->attachDocument(*document);
  static const DummyLiteral literal;
  CstBuilder builder(*cst);
  builder.leaf(0, static_cast<TextOffset>(refText.size()), &literal);

  static const TestReferenceAssignment<Holder, TargetType> assignment("ref");
  document->parseResult.astArena = std::make_unique<pegium::AstArena>(*cst);

  auto &arena = *document->parseResult.astArena;
  arena.attachDocument(*document);
  auto *holder = arena.create<Holder>();
  holder->setCstNode(cst->get(0));
  holder->ref.initialize(*holder, refText, cst->get(0), assignment, linker);

  document->parseResult.value = holder;
  document->parseResult.cst = std::move(cst);
  document->parseResult.references.push_back(ReferenceHandle::direct(&holder->ref));
  return {.document = std::move(document), .holder = holder};
}

std::vector<std::string>
collect_names(const ScopeProvider &scopeProvider, const ReferenceInfo &info) {
  std::vector<std::string> names;
  const auto collectEntry = [&names](const workspace::AstNodeDescription &entry) {
    names.push_back(entry.name);
    return true;
  };
  const auto completed = scopeProvider.visitScopeEntries(
      info,
      utils::function_ref<bool(const workspace::AstNodeDescription &)>(
          collectEntry));
  EXPECT_TRUE(completed);
  return names;
}

TEST(DefaultScopeProviderTest, InvalidatesGlobalScopeCacheWhenDocumentChanges) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto *builder = new test::RecordingEventDocumentBuilder();
  shared->workspace.documentBuilder.reset(builder);
  auto *indexManager = new TestIndexManager(shared->astReflection.get());
  shared->workspace.indexManager.reset(indexManager);

  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);

  auto fixture = make_attached_reference_holder<RefHolder, TargetNode>(
      *shared, *linker, test::make_file_uri("scope-provider-change.test"),
      "first");
  const auto documentId = fixture.document->id;
  const auto refType = std::type_index(typeid(TargetNode));

  indexManager->setExports(
      documentId,
      {{.name = "first", .type = refType, .documentId = documentId}});

  auto allInfo = makeReferenceInfo(fixture.holder->ref);
  allInfo.referenceText = {};
  EXPECT_EQ(collect_names(*scopeProvider, allInfo),
            (std::vector<std::string>{"first"}));

  indexManager->setExports(
      documentId,
      {{.name = "second", .type = refType, .documentId = documentId}});
  builder->emitUpdate({documentId}, {});

  EXPECT_EQ(collect_names(*scopeProvider, allInfo),
            (std::vector<std::string>{"second"}));
}

TEST(DefaultScopeProviderTest, InvalidatesGlobalScopeCacheWhenDocumentIsDeleted) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto *builder = new test::RecordingEventDocumentBuilder();
  shared->workspace.documentBuilder.reset(builder);
  auto *indexManager = new TestIndexManager(shared->astReflection.get());
  shared->workspace.indexManager.reset(indexManager);

  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);

  auto fixture = make_attached_reference_holder<RefHolder, TargetNode>(
      *shared, *linker, test::make_file_uri("scope-provider-delete.test"),
      "value");
  const auto documentId = fixture.document->id;
  const auto refType = std::type_index(typeid(TargetNode));

  indexManager->setExports(
      documentId,
      {{.name = "value", .type = refType, .documentId = documentId}});

  auto allInfo = makeReferenceInfo(fixture.holder->ref);
  allInfo.referenceText = {};
  EXPECT_EQ(collect_names(*scopeProvider, allInfo),
            (std::vector<std::string>{"value"}));

  EXPECT_TRUE(shared->workspace.indexManager->remove(documentId));
  builder->emitUpdate({}, {documentId});

  EXPECT_TRUE(collect_names(*scopeProvider, allInfo).empty());
}

TEST(DefaultScopeProviderTest, ResolvesRegisteredSubtypesThroughGlobalEntries) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto *indexManager = new TestIndexManager(shared->astReflection.get());
  shared->workspace.indexManager.reset(indexManager);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);
  ASSERT_NE(shared->astReflection.get(), nullptr);

  auto fixture = make_attached_reference_holder<BaseRefHolder, BaseTargetNode>(
      *shared, *linker,
      test::make_file_uri("scope-provider-dynamic-subtype.test"), "derived");
  const auto documentId = fixture.document->id;

  indexManager->setExports(
      documentId,
      {{.name = "derived",
        .type = std::type_index(typeid(DerivedTargetNode)),
        .documentId = documentId}});

  ScopeSubtypeBootstrapParser parser;
  bootstrapAstReflection(static_cast<const Parser &>(parser).getEntryRule(),
                         *shared->astReflection);

  const auto *resolved =
      scopeProvider->getScopeEntry(makeReferenceInfo(fixture.holder->ref));
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->name, "derived");
}

TEST(DefaultScopeProviderTest,
     ResolvesSyntheticReferenceInfoUsingAssignmentMetadata) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto *indexManager = new TestIndexManager(shared->astReflection.get());
  shared->workspace.indexManager.reset(indexManager);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);

  auto fixture = make_attached_reference_holder<RefHolder, TargetNode>(
      *shared, *linker, test::make_file_uri("scope-provider-synthetic.test"),
      "visible");
  const auto documentId = fixture.document->id;
  TestReferenceAssignment<RefHolder, TargetNode> assignment("ref");

  indexManager->setExports(
      documentId,
      {{.name = "visible",
        .type = std::type_index(typeid(TargetNode)),
        .documentId = documentId}});

  const ReferenceInfo info{fixture.holder, {}, assignment};
  EXPECT_EQ(collect_names(*scopeProvider, info),
            (std::vector<std::string>{"visible"}));
}

TEST(DefaultScopeProviderTest,
     VisitsLocalScopesBeforeGlobalScopeAndPreservesNearestFirstOrder) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto *indexManager = new TestIndexManager(shared->astReflection.get());
  shared->workspace.indexManager.reset(indexManager);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);

  auto fixture = make_scoped_document(*shared, *linker,
                                      test::make_file_uri("scope-order.test"));
  fixture.document->localSymbols.emplace(
      fixture.outer,
      workspace::AstNodeDescription{.name = "outer",
                                    .type = std::type_index(typeid(TargetNode)),
                                    .documentId = fixture.document->id,
                                    .symbolId = 1});
  fixture.document->localSymbols.emplace(
      fixture.inner,
      workspace::AstNodeDescription{.name = "inner",
                                    .type = std::type_index(typeid(TargetNode)),
                                    .documentId = fixture.document->id,
                                    .symbolId = 2});
  indexManager->setExports(
      fixture.document->id,
      {{.name = "global-1",
        .type = std::type_index(typeid(TargetNode)),
        .documentId = fixture.document->id,
        .symbolId = 3},
       {.name = "global-2",
        .type = std::type_index(typeid(TargetNode)),
        .documentId = fixture.document->id,
        .symbolId = 4}});

  auto info = makeReferenceInfo(fixture.holder->ref);
  info.referenceText = {};
  EXPECT_EQ(collect_names(*scopeProvider, info),
            (std::vector<std::string>{"inner", "outer", "global-1",
                                      "global-2"}));
}

TEST(DefaultScopeProviderTest,
     VisitsMatchingDuplicatesAcrossLocalScopesAndGlobalScopeInOrder) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto *indexManager = new TestIndexManager(shared->astReflection.get());
  shared->workspace.indexManager.reset(indexManager);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);

  auto fixture = make_scoped_document(*shared, *linker,
                                      test::make_file_uri("scope-duplicates.test"),
                                      "dup");
  fixture.document->localSymbols.emplace(
      fixture.outer,
      workspace::AstNodeDescription{.name = "dup",
                                    .type = std::type_index(typeid(TargetNode)),
                                    .documentId = fixture.document->id,
                                    .symbolId = 1});
  fixture.document->localSymbols.emplace(
      fixture.inner,
      workspace::AstNodeDescription{.name = "dup",
                                    .type = std::type_index(typeid(TargetNode)),
                                    .documentId = fixture.document->id,
                                    .symbolId = 2});
  indexManager->setExports(
      fixture.document->id,
      {{.name = "dup",
        .type = std::type_index(typeid(TargetNode)),
        .documentId = fixture.document->id,
        .symbolId = 3}});

  EXPECT_EQ(collect_names(*scopeProvider, makeReferenceInfo(fixture.holder->ref)),
            (std::vector<std::string>{"dup", "dup", "dup"}));
}

TEST(DefaultScopeProviderTest, StopsVisitingAsSoonAsVisitorReturnsFalse) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto *indexManager = new TestIndexManager(shared->astReflection.get());
  shared->workspace.indexManager.reset(indexManager);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);

  auto fixture = make_scoped_document(*shared, *linker,
                                      test::make_file_uri("scope-stop.test"));
  fixture.document->localSymbols.emplace(
      fixture.inner,
      workspace::AstNodeDescription{.name = "first",
                                    .type = std::type_index(typeid(TargetNode)),
                                    .documentId = fixture.document->id,
                                    .symbolId = 1});
  fixture.document->localSymbols.emplace(
      fixture.outer,
      workspace::AstNodeDescription{.name = "second",
                                    .type = std::type_index(typeid(TargetNode)),
                                    .documentId = fixture.document->id,
                                    .symbolId = 2});

  auto info = makeReferenceInfo(fixture.holder->ref);
  info.referenceText = {};

  std::vector<std::string> visited;
  const auto collectEntry =
      [&visited](const workspace::AstNodeDescription &entry) {
        visited.push_back(entry.name);
        return false;
      };
  const auto completed = scopeProvider->visitScopeEntries(
      info,
      utils::function_ref<bool(const workspace::AstNodeDescription &)>(
          collectEntry));
  EXPECT_FALSE(completed);
  EXPECT_EQ(visited, (std::vector<std::string>{"first"}));
}

TEST(DefaultScopeProviderTest, AllowsDerivedProviderToOverrideGlobalEntriesHook) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);
  services->references.scopeProvider =
      std::make_unique<OverridingGlobalScopeProvider>(
          *services,
          std::vector<workspace::AstNodeDescription>{
              {.name = "hooked",
               .type = std::type_index(typeid(TargetNode)),
               .documentId = 1,
               .symbolId = 1}});
  auto *scopeProvider = services->references.scopeProvider.get();
  auto *linker = services->references.linker.get();
  ASSERT_NE(scopeProvider, nullptr);
  ASSERT_NE(linker, nullptr);

  auto fixture = make_attached_reference_holder<RefHolder, TargetNode>(
      *shared, *linker, test::make_file_uri("scope-provider-hooked.test"),
      "hooked");
  TestReferenceAssignment<RefHolder, TargetNode> assignment("ref");
  const ReferenceInfo info{fixture.holder, {}, assignment};

  EXPECT_EQ(collect_names(*scopeProvider, info),
            (std::vector<std::string>{"hooked"}));
}

} // namespace
} // namespace pegium::references
