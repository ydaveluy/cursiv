// The workspace: documents, their text and their factory, the file systems
// under them, the index and the local symbols, the build that carries a
// document through its states, and the scan that finds them.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <latch>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/core/execution/TaskScheduler.hpp"
#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/references/DefaultScopeComputation.hpp"
#include "cursiv/core/references/DefaultScopeProvider.hpp"
#include "cursiv/core/references/Linker.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/DefaultServiceRegistry.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/DefaultAstReflection.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/utils/Utf8.hpp"
#include "cursiv/core/validation/DocumentValidator.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/workspace/AstNodeLocator.hpp"
#include "cursiv/core/workspace/DefaultDocumentFactory.hpp"
#include "cursiv/core/workspace/DefaultDocuments.hpp"
#include "cursiv/core/workspace/DefaultIndexManager.hpp"
#include "cursiv/core/workspace/DefaultReferenceDescriptionProvider.hpp"
#include "cursiv/core/workspace/DefaultWorkspaceManager.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "cursiv/core/workspace/LocalSymbols.hpp"
#include "cursiv/core/workspace/MemoryFileSystemProvider.hpp"
#include "cursiv/core/workspace/ReferenceDescriptionProvider.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/core/workspace/WorkspaceFolder.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/core/ast.hpp"
#include "workspace/generated/Workspace/Module.hpp"

#if !defined(NDEBUG) && !defined(__wasi__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#endif

// A path names the node it was taken from, and nothing else. The one trap is
// where a path starts: the AST root's container is the document, which holds it
// in no member of its own.
namespace ast_node_locator {

namespace {

struct World {
  cursiv::SharedCoreServices shared;
  const cursiv::AstNodeLocator* locator = nullptr;

  World() {
    cursiv::install_default_shared_core_services(shared);
    auto language = std::make_unique<cursiv::CoreServices>(shared);
    language->languageMetaData = {"workspace", {".ws"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    language->astReflection =
        ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    locator = language->astNodeLocator.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }

  [[nodiscard]] std::shared_ptr<cursiv::Document> build(
      std::string text) const {
    const std::shared_ptr<cursiv::Document> document =
        shared.documentFactory->fromString(std::move(text), "file:///l.ws");
    shared.documents->addDocument(document);
    cursiv::Document* const one[] = {document.get()};
    shared.documentBuilder->build(one);
    return document;
  }
};

// Every node of a real tree, written down and read back. Nesting matters: a
// path that reads back correctly at one level could still be wrong at the
// document-rooted step above it.
void a_path_reads_back_the_node_it_names() {
  const World world;
  const std::shared_ptr<cursiv::Document> document = world.build(
      "datatype String\npackage p {\n entity E { a: String\n b: String }\n}\n");

  int checked = 0;
  for (const cursiv::AstNode* const node : document->parseResult.nodes) {
    const std::string path = world.locator->getAstNodePath(*node);
    if (path.empty()) continue;  // the root names itself by the empty path
    ++checked;
    CHECK(world.locator->getAstNode(*document->parseResult.value, path) ==
          node);
  }
  // A tree of five named nodes: the datatype, the package, the entity, and its
  // two features.
  CHECK(checked == 5);
}

// The root's path is empty and that empty path reads back as the root -- the
// pair is what makes the convention sound rather than merely consistent.
void the_root_names_itself_by_nothing() {
  const World world;
  const std::shared_ptr<cursiv::Document> document =
      world.build("datatype String\n");
  cursiv::AstNode* const root = document->parseResult.value;

  CHECK(world.locator->getAstNodePath(*root).empty());
  CHECK(world.locator->getAstNode(*root, "") == root);
}

void a_detached_tree_keeps_its_paths() {
  const World world;
  const auto document = world.build("datatype String\n");
  auto* root = static_cast<ws::ast::Workspace*>(document->parseResult.value);
  root->parent = nullptr;
  CHECK(world.locator->getAstNodePath(*root).empty());
  CHECK(world.locator->getAstNodePath(*root->elements[0]) == "/elements@0");
  CHECK(world.locator->getAstNode(*root, "/elements@0") == root->elements[0]);
}

// A node its container does not hold has no path, and says so. Yielding an
// empty one would read back as the root: a wrong node, handed over in silence.
void an_unheld_node_refuses_to_have_a_path() {
  const World world;
  const std::shared_ptr<cursiv::Document> document =
      world.build("datatype String\n");

  ws::ast::DataType orphan;
  orphan.parent = document->parseResult.value;  // claims a container...
  // ...which holds it nowhere.
  bool refused = false;
  try {
    (void)world.locator->getAstNodePath(orphan);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  CHECK(refused);
}

}  // namespace

void run() {
  a_path_reads_back_the_node_it_names();
  the_root_names_itself_by_nothing();
  a_detached_tree_keeps_its_paths();
  an_unheld_node_refuses_to_have_a_path();
}

}  // namespace ast_node_locator

// Pins the ladder as v5 climbs it: four phases, each spread over the pool,
// each announced once drained -- and what a change takes back down first.
namespace document_builder {

namespace {

// Heard on the calling thread, once per rung the build publishes.
struct Heard : cursiv::BuildPhaseListener {
  std::vector<cursiv::DocumentState> rungs;
  std::vector<std::size_t> counts;

  void onBuildPhase(cursiv::DocumentState reached,
                    std::span<cursiv::Document* const> built,
                    const cursiv::CancellationToken&) override {
    rungs.push_back(reached);
    counts.push_back(built.size());
  }
};

struct World {
  cursiv::SharedCoreServices shared;

  // `tweak` replaces a slot after the defaults are in, before registration.
  explicit World(const std::function<void(cursiv::CoreServices&)>& tweak = {}) {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_core_services(shared);
    auto language = std::make_unique<cursiv::CoreServices>(shared);
    language->languageMetaData = {"workspace", {".ws"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    if (tweak) tweak(*language);
    shared.serviceRegistry->registerServices(std::move(language));
  }

  cursiv::Document* add(const std::string& uri, std::string text) const {
    auto document =
        shared.documentFactory->fromString(std::move(text), std::move(uri));
    cursiv::Document* const raw = document.get();
    shared.documents->addDocument(std::move(document));
    return raw;
  }
};

void a_default_workspace_build_uses_its_workers() {
  World world;
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(2);
  struct Together final : cursiv::DocumentPhaseListener {
    std::atomic<unsigned> entered{0};
    std::atomic<bool> overlapped{true};
    std::promise<void> both;
    std::shared_future<void> ready = both.get_future().share();

    void onDocumentPhase(cursiv::Document&, cursiv::DocumentState reached,
                         const cursiv::CancellationToken&) override {
      if (reached != cursiv::DocumentState::Parsed) return;
      if (entered.fetch_add(1) == 1) both.set_value();
      // Two workers, two documents: they must make progress together. A
      // timeout reports an accidental serial build without hanging.
      if (ready.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        overlapped = false;
    }
  } together;
  cursiv::Document* documents[] = {
      world.add("file:///a.ws", "datatype A\n"),
      world.add("file:///b.ws", "datatype B\n")};
  for (auto* document : documents)
    world.shared.documentBuilder->resetToState(*document,
                                               cursiv::DocumentState::Changed);
  world.shared.documentBuilder->addDocumentPhaseListener(together);
  world.shared.documentBuilder->build(documents);
  world.shared.documentBuilder->removeDocumentPhaseListener(together);
  CHECK(together.entered == 2);
  CHECK(together.overlapped);
  for (const auto* document : documents)
    CHECK(document->state.load() == cursiv::DocumentState::Validated);
}

#if !defined(__wasi__) || defined(_REENTRANT)
void exported_symbols_are_collected_in_parallel() {
  struct Together final : cursiv::DefaultScopeComputation {
    using DefaultScopeComputation::DefaultScopeComputation;
    mutable std::latch entered{2};
    mutable std::atomic<unsigned> calls{0};
    mutable std::atomic<bool> overlapped{true};
    std::vector<cursiv::AstNodeDescription> collectExportedSymbols(
        const cursiv::Document& document,
        const cursiv::CancellationToken& token) const override {
      if (calls.fetch_add(1) < 2) entered.count_down();
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!entered.try_wait() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
      if (!entered.try_wait()) overlapped = false;
      return DefaultScopeComputation::collectExportedSymbols(document, token);
    }
  };
  Together* observed = nullptr;
  World world([&](cursiv::CoreServices& language) {
    auto scopes = std::make_unique<Together>(language);
    observed = scopes.get();
    language.scopeComputation = std::move(scopes);
  });
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(2);
  cursiv::Document* documents[] = {
      world.add("file:///b.ws", "datatype B"),
      world.add("file:///a.ws", "datatype A")};
  world.shared.documentBuilder->build(documents);
  CHECK(observed->calls == 2);
  CHECK(observed->entered.try_wait());
  CHECK(observed->overlapped);
}
#endif

void exports_keep_uri_order(bool interrupt) {
  std::stop_source cancellation;
  World world;
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(2);
  struct Indexed final : cursiv::DocumentPhaseListener {
    std::mutex mutex;
    std::vector<cursiv::DocumentId> order;
    std::stop_source* cancellation;
    explicit Indexed(std::stop_source* stop) : cancellation(stop) {}
    void onDocumentPhase(cursiv::Document& document, cursiv::DocumentState state,
                         const cursiv::CancellationToken&) override {
      if (state != cursiv::DocumentState::IndexedContent) return;
      const std::lock_guard lock(mutex);
      order.push_back(document.id);
      if (cancellation != nullptr && document.uri == "file:///first.ws")
        cancellation->request_stop();
    }
  } indexed(interrupt ? &cancellation : nullptr);
  cursiv::Document* second = world.add("file:///second.ws", "datatype Same");
  cursiv::Document* first = world.add("file:///first.ws", "datatype Same");
  cursiv::Document* use =
      world.add("file:///use.ws", "entity Uses { f : Same }");
  cursiv::Document* documents[] = {second, first, use};
  world.shared.documentBuilder->addDocumentPhaseListener(indexed);
  bool cancelled = false;
  try {
    world.shared.documentBuilder->build(documents, {}, cancellation.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled == interrupt);
  if (cancelled) world.shared.documentBuilder->build(documents);
  world.shared.documentBuilder->removeDocumentPhaseListener(indexed);
  auto expected = std::vector{first->id, second->id, use->id};
  std::ranges::sort(expected);
  std::ranges::sort(indexed.order);
  CHECK(indexed.order == expected);
  const auto* winner = world.shared.indexManager->findByName(
      "Same", true, {}, *first->language->astReflection);
  CHECK(winner != nullptr);
  CHECK(winner != nullptr && winner->documentId == first->id);
  CHECK(!use->hasLinkingErrors());
  CHECK(use->diagnostics.empty());
  const auto* entity = cursiv::ast_cast<ws::ast::Entity>(
      use->parseResult.value_as<ws::ast::Workspace>()->elements[0]);
  const auto* target = entity->features[0]->type.get();
  CHECK(target != nullptr);
  CHECK(target != nullptr && target->getDocument() == first);
}

void an_update_honors_an_overridden_affected_check() {
  World world;
  struct Affected final : cursiv::DefaultIndexManager {
    using DefaultIndexManager::DefaultIndexManager;
    mutable std::atomic<unsigned> calls{0};
    cursiv::DocumentId forced = cursiv::kNoDocument;
    bool isAffected(const cursiv::Document& document,
                    const std::unordered_set<cursiv::DocumentId>& changed)
        const override {
      ++calls;
      return document.id == forced ||
             DefaultIndexManager::isAffected(document, changed);
    }
  };
  auto index = std::make_unique<Affected>(world.shared);
  Affected* observed = index.get();
  world.shared.indexManager = std::move(index);
  cursiv::Document* target = world.add("file:///target.ws", "datatype Type");
  cursiv::Document* dependent =
      world.add("file:///dependent.ws", "entity E { f : Type }");
  cursiv::Document* unrelated =
      world.add("file:///unrelated.ws", "datatype Other");
  cursiv::Document* documents[] = {target, dependent, unrelated};
  world.shared.documentBuilder->build(documents);
  observed->forced = unrelated->id;
  observed->calls = 0;
  struct Relinked final : cursiv::DocumentPhaseListener {
    cursiv::DocumentId forced = cursiv::kNoDocument;
    std::atomic<unsigned> calls{0};
    void onDocumentPhase(cursiv::Document& document, cursiv::DocumentState state,
                         const cursiv::CancellationToken&) override {
      if (document.id == forced && state == cursiv::DocumentState::Linked)
        ++calls;
    }
  } relinked;
  relinked.forced = unrelated->id;
  world.shared.documentBuilder->addDocumentPhaseListener(relinked);
  const std::string_view changed[] = {target->uri};
  world.shared.documentBuilder->update(changed, {});
  world.shared.documentBuilder->removeDocumentPhaseListener(relinked);
  CHECK(observed->calls >= 1);
  CHECK(relinked.calls == 1);
  CHECK(!dependent->hasLinkingErrors());
  const std::unordered_set<cursiv::DocumentId> moved = {dependent->id, target->id};
  CHECK(world.shared.indexManager->isAffected(*dependent, moved));
}

class CountingParser final : public cursiv::Parser {
 public:
  explicit CountingParser(std::unique_ptr<const cursiv::Parser> parser)
      : _parser(std::move(parser)) {}
  const cursiv::Grammar& getGrammar() const noexcept override {
    return _parser->getGrammar();
  }
  cursiv::ParseResult parse(std::string_view text,
                            const cursiv::CancellationToken& token) const override {
    ++calls;
    return _parser->parse(text, token);
  }
  mutable std::atomic<unsigned> calls{0};
 private:
  std::unique_ptr<const cursiv::Parser> _parser;
};

void factory_then_build_parses_once() {
  CountingParser* counter = nullptr;
  const World world([&](cursiv::CoreServices& language) {
    auto parser = std::make_unique<CountingParser>(std::move(language.parser));
    counter = parser.get();
    language.parser = std::move(parser);
  });
  cursiv::Document* const document = world.add("file:///a.ws", "datatype A\n");
  CHECK(counter->calls.load() == 1);
  CHECK(document->state.load() == cursiv::DocumentState::Parsed);
  const cursiv::AstNode* const root = document->parseResult.value;
  cursiv::Document* const set[] = {document};
  world.shared.documentBuilder->build(set);
  CHECK(counter->calls.load() == 1);
  CHECK(document->parseResult.value == root);
  CHECK(document->state.load() == cursiv::DocumentState::Validated);
}

// A legal provider can follow a candidate's own reference, as an inherited
// or qualified scope does. Every home must be ready before the first link.
struct FollowingScope final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;
  std::latch* entered = nullptr;
  std::latch* release = nullptr;
  const cursiv::AstNodeDescription* getScopeEntry(
      const cursiv::ReferenceInfo& context, std::string_view name) const override {
    if (name == "C" && entered != nullptr) {
      entered->count_down();
      release->wait();
    }
    if (name == "B" && entered != nullptr) entered->wait();
    const auto* found = DefaultScopeProvider::getScopeEntry(context, name);
    if (found == nullptr) return nullptr;
    const auto* entity = cursiv::ast_cast<ws::ast::Entity>(found->node);
    if (entity != nullptr && !entity->superType.refText.empty() &&
        entity->superType.get() == nullptr)
      return nullptr;
    return found;
  }
};

void concurrent_linking_waits_for_a_candidates_own_reference() {
  using namespace std::chrono_literals;
  std::latch entered(1), release(1);
  World world([&](cursiv::CoreServices& language) {
    auto scope = std::make_unique<FollowingScope>(language);
    scope->entered = &entered;
    scope->release = &release;
    language.scopeProvider = std::move(scope);
  });
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(2);
  auto* a = world.add("file:///a.ws", "entity A extends B { }");
  auto* b = world.add("file:///b.ws", "entity B extends C { } entity C { }");
  auto* aa = cursiv::ast_cast<ws::ast::Entity>(
      a->parseResult.value_as<ws::ast::Workspace>()->elements[0]);
  auto* bb = cursiv::ast_cast<ws::ast::Entity>(
      b->parseResult.value_as<ws::ast::Workspace>()->elements[0]);
  cursiv::Document* documents[] = {a, b};
  auto build = std::async(std::launch::async, [&] {
    world.shared.documentBuilder->build(documents);
  });
  entered.wait();
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  const std::atomic_ref<cursiv::RefState> state(bb->superType._state);
  while (state.load() != cursiv::detail::kRefObserved &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  CHECK(state.load() == cursiv::detail::kRefObserved);
  release.count_down();
  build.get();
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(b->state.load() == cursiv::DocumentState::Validated);
  CHECK(a->diagnostics.empty() && b->diagnostics.empty());
  CHECK(aa->superType.get() == bb && bb->superType.get() != nullptr);
}

void references_to_a_later_document_can_resolve_lazily() {
  World world([](cursiv::CoreServices& language) {
    language.scopeProvider = std::make_unique<FollowingScope>(language);
  });
  // No race is needed: the first document depends on the second, which has
  // not reached its own link() call when the first one follows B's reference.
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(0);
  auto* a = world.add("file:///a.ws", "entity A extends B { }");
  auto* b = world.add("file:///b.ws", "entity B extends C { } entity C { }");
  cursiv::Document* documents[] = {a, b};
  const auto check = [&] {
    CHECK(a->state.load() == cursiv::DocumentState::Validated);
    CHECK(b->state.load() == cursiv::DocumentState::Validated);
    CHECK(a->diagnostics.empty() && b->diagnostics.empty());
    const auto* aa = cursiv::ast_cast<ws::ast::Entity>(
        a->parseResult.value_as<ws::ast::Workspace>()->elements[0]);
    const auto* bb = cursiv::ast_cast<ws::ast::Entity>(
        b->parseResult.value_as<ws::ast::Workspace>()->elements[0]);
    CHECK(aa->superType.get() == bb);
    CHECK(bb->superType.get() != nullptr);
  };
  world.shared.documentBuilder->build(documents);
  check();
  // Also cover a resumed build: its retained scopes are not computed again.
  for (auto* d : documents)
    world.shared.documentBuilder->resetToState(
        *d, cursiv::DocumentState::ComputedScopes);
  world.shared.documentBuilder->build(documents);
  check();
}

// Follows each entity's parent and reads the parent's own reference while the
// pool scopes the parent's document: a read made before that document's
// scopes, which must not be kept.
struct HuntingScopes final : cursiv::DefaultScopeComputation {
  using DefaultScopeComputation::DefaultScopeComputation;
  [[nodiscard]] cursiv::LocalSymbols collectLocalSymbols(
      const cursiv::Document& document,
      const cursiv::CancellationToken& token) const override {
    for (const auto* element :
         document.parseResult.value_as<ws::ast::Workspace>()->elements)
      if (const auto* entity = cursiv::ast_cast<ws::ast::Entity>(element))
        if (const auto* found = services.shared.indexManager->findByName(
                entity->superType.refText, true, {}, *services.astReflection))
          if (const auto* parent = cursiv::ast_cast<ws::ast::Entity>(found->node))
            for (int k = 0; k < 8; ++k) {
              (void)parent->superType.state();
              std::this_thread::yield();
            }
    return DefaultScopeComputation::collectLocalSymbols(document, token);
  }
};

void a_reference_read_while_scoping_is_read_again() {
  constexpr int kChain = 8;
#ifdef __wasi__
  constexpr int kRounds = 10;  // a wasm thread is a worker started per pool
#else
  constexpr int kRounds = 200;
#endif
  for (int round = 0; round < kRounds; ++round) {
    World world([](cursiv::CoreServices& language) {
      language.scopeComputation = std::make_unique<HuntingScopes>(language);
    });
    world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(4);
    std::vector<cursiv::Document*> chain;
    for (int i = 0; i < kChain; ++i)
      chain.push_back(world.add(
          "file:///e" + std::to_string(i) + ".ws",
          "entity E" + std::to_string(i) +
              (i + 1 < kChain ? " extends E" + std::to_string(i + 1) : "") +
              " { }\n"));
    world.shared.documentBuilder->build(chain);
    int kept = 0;
    for (int i = 0; i + 1 < kChain; ++i)
      if (cursiv::ast_cast<ws::ast::Entity>(
              chain[i]->parseResult.value_as<ws::ast::Workspace>()->elements[0])
              ->superType.state() != cursiv::RefState::Resolved)
        ++kept;
    CHECK(kept == 0);
    if (kept != 0) return;
  }
}

void a_build_climbs_every_rung() {
  const World world;
  cursiv::Document* const document =
      world.add("file:///a.ws", "datatype String\nentity E { f : String }\n");
  cursiv::Document* const set[] = {document};

  world.shared.documentBuilder->build(set);
  CHECK(document->state.load() == cursiv::DocumentState::Validated);
  CHECK(!document->localSymbols.empty());

  // The reference resolved, and the index knows the edge.
  std::size_t edges = 0;
  (void)world.shared.indexManager->visitElements(
      nullptr, {}, [&](const cursiv::AstNodeDescription& d) {
        (void)world.shared.indexManager->visitReferences(
            d.documentId, d.symbolId, [&](const cursiv::ReferenceDescription&) {
              ++edges;
              return true;
            });
        return true;
      });
  CHECK(edges == 1);
  // Nothing to complain about, so nothing was published.
  CHECK(document->diagnostics.empty());
}

// Four phases, and each announces its rungs once drained -- never one document
// at a time, because a phase may read what another published below it.
void the_rungs_are_announced_in_order() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype String\n");
  cursiv::Document* const b = world.add("file:///b.ws", "datatype Other\n");
  cursiv::Document* const set[] = {a, b};

  Heard heard;
  world.shared.documentBuilder->addBuildPhaseListener(heard);
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeBuildPhaseListener(heard);

  // Exports follow parsing, then scopes, linking, reference indexing and checks.
  CHECK(heard.rungs.size() == 6);
  CHECK(heard.rungs[0] == cursiv::DocumentState::Parsed);
  CHECK(heard.rungs[1] == cursiv::DocumentState::IndexedContent);
  CHECK(heard.rungs[2] == cursiv::DocumentState::ComputedScopes);
  CHECK(heard.rungs[3] == cursiv::DocumentState::Linked);
  CHECK(heard.rungs[4] == cursiv::DocumentState::IndexedReferences);
  CHECK(heard.rungs[5] == cursiv::DocumentState::Validated);
  // The factory already parsed them, and the `Parsed` rung still names both:
  // a rung is announced with every document standing on it.
  for (const std::size_t count : heard.counts) CHECK(count == 2);
}

// A broken reference is a diagnostic, and the repair carries its quick fixes.
void what_does_not_resolve_is_said() {
  const World world;
  cursiv::Document* const document =
      world.add("file:///a.ws", "datatype String\nentity E { f : Absent }\n");
  cursiv::Document* const set[] = {document};
  world.shared.documentBuilder->build(set);

  CHECK(document->diagnostics.size() == 1);
  const cursiv::Diagnostic& d = document->diagnostics.front();
  CHECK(d.severity == cursiv::DiagnosticSeverity::Error);
  // The type the member wanted is in the sentence, which is the half a reader
  // needs to satisfy the member.
  CHECK(d.message == "Could not resolve reference to Type named 'Absent'.");
  // Anchored on the name: the word the editor underlines is the one read.
  CHECK(document->textDocument().getText().substr(
            d.range.begin, d.range.end - d.range.begin) == "Absent");
  // The machine's half: the code names the failure class, `data` carries the
  // written text so a quick fix needs no English parsed back out.
  CHECK(d.code.has_value());
  if (d.code.has_value()) CHECK(*d.code == cursiv::kLinkingError);
  CHECK(d.data.has_value() && d.data->isObject());
  if (d.data.has_value() && d.data->isObject()) {
    const auto at = d.data->object().find(cursiv::kLinkingRefText);
    CHECK(at != d.data->object().end());
    if (at != d.data->object().end())
      CHECK(at->second.isString() && at->second.string() == "Absent");
  }
}

// Going back down takes what was derived with it.
void a_reset_forgets_what_was_derived() {
  const World world;
  cursiv::Document* const a =
      world.add("file:///a.ws", "datatype String\nentity E { f : String }\n");
  cursiv::Document* const set[] = {a};
  world.shared.documentBuilder->build(set);
  CHECK(!a->localSymbols.empty());

  world.shared.documentBuilder->resetToState(*a,
                                             cursiv::DocumentState::Changed);
  CHECK(a->state.load() == cursiv::DocumentState::Changed);
  CHECK(a->localSymbols.empty());
  std::size_t exported = 0;
  (void)world.shared.indexManager->visitElements(
      nullptr, {}, [&exported](const cursiv::AstNodeDescription&) {
        ++exported;
        return true;
      });
  CHECK(exported == 0);
}

// A change rebuilds what aimed at it, and leaves the rest alone.
void an_update_rebuilds_what_aimed_at_the_change() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype String\n");
  cursiv::Document* const b = world.add("file:///b.ws", "datatype Other\n");
  cursiv::Document* const set[] = {a, b};
  world.shared.documentBuilder->build(set);

  Heard heard;
  world.shared.documentBuilder->addBuildPhaseListener(heard);
  const std::string_view changed[] = {"file:///a.ws"};
  world.shared.documentBuilder->update(changed, {});
  world.shared.documentBuilder->removeBuildPhaseListener(heard);

  // Only `a` fell: `b` names nothing of it.
  CHECK(!heard.counts.empty());
  CHECK(heard.counts.front() == 1);
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(b->state.load() == cursiv::DocumentState::Validated);
}

// `Documents.hpp` requires the caller to purge indexes and unlink dependents
// before a document that is still referenced dies -- `update` is the safe
// route. This pins what a debug build's guard in
// `DefaultDocuments::deleteDocument` reads: the incoming edge at the deleted
// document's symbol is gone before the row is dropped, not merely eventually.
void an_update_clears_the_incoming_edge_before_deleting_its_target() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype Shared\n");
  cursiv::Document* const b =
      world.add("file:///b.ws", "entity E { f : Shared }\n");
  cursiv::Document* const set[] = {a, b};
  world.shared.documentBuilder->build(set);
  CHECK(!b->hasLinkingErrors());

  const cursiv::AstNodeDescription* shared = nullptr;
  (void)world.shared.indexManager->visitElements(
      nullptr, {}, [&shared](const cursiv::AstNodeDescription& d) {
        if (d.name == "Shared") shared = &d;
        return shared == nullptr;
      });
  CHECK(shared != nullptr);
  if (shared == nullptr) return;
  const cursiv::DocumentId aId = shared->documentId;
  const cursiv::NodeId sharedSymbol = shared->symbolId;
  const auto unreferenced = [&] {
    return world.shared.indexManager->visitReferences(
        aId, sharedSymbol,
        [](const cursiv::ReferenceDescription&) { return false; });
  };
  CHECK(!unreferenced());

  const std::string_view deleted[] = {"file:///a.ws"};
  world.shared.documentBuilder->update({}, deleted);

  CHECK(world.shared.documents->getDocument("file:///a.ws") == nullptr);
  CHECK(unreferenced());
  CHECK(b->hasLinkingErrors());
}

// A reset takes the request with it: `a`, asked for no validating and
// carrying a broken reference, is relinked by someone else's update and
// rebuilt under the update's request -- its own went with the reset.
void a_relinked_document_takes_the_updates_request() {
  const World world;
  cursiv::Document* const a =
      world.add("file:///a.ws", "entity E { f : Missing }\n");
  cursiv::Document* const setA[] = {a};
  world.shared.documentBuilder->build(setA, cursiv::BuildOptions{false, {}});

  CHECK(a->state.load() == cursiv::DocumentState::IndexedReferences);
  CHECK(a->diagnostics.empty());  // asked for none, published none

  cursiv::Document* const b = world.add("file:///b.ws", "datatype Other\n");
  cursiv::Document* const setB[] = {b};
  world.shared.documentBuilder->build(setB);
  const std::string_view changed[] = {"file:///b.ws"};
  world.shared.documentBuilder->update(changed, {});

  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(!a->diagnostics.empty());
  CHECK(b->state.load() == cursiv::DocumentState::Validated);
}

// An interrupted request does not outlive the text it was made for: the text
// moves, the reset takes the request, and the rebuild carries the update's.
void a_reset_forgets_an_interrupted_request() {
  const World world;
  cursiv::Document* const a =
      world.add("file:///a.ws", "entity E { f : Missing }\n");
  cursiv::Document* const set[] = {a};
  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool cancelled = false;
  try {
    world.shared.documentBuilder->build(set, cursiv::BuildOptions{false, {}},
                                        source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  const std::string_view changed[] = {"file:///a.ws"};
  world.shared.documentBuilder->update(changed, {});
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(!a->diagnostics.empty());
}

// A request cut after its last rung is finished by the next update, however
// empty: left unfinished, it would hold off every later request for ever.
void an_update_finishes_an_interrupted_request() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype D\n");
  cursiv::Document* const set[] = {a};
  cursiv::CancellationTokenSource source;
  struct CutAt final : cursiv::BuildPhaseListener {
    cursiv::CancellationTokenSource* source = nullptr;
    void onBuildPhase(cursiv::DocumentState reached,
                      std::span<cursiv::Document* const>,
                      const cursiv::CancellationToken&) override {
      if (reached == cursiv::DocumentState::IndexedReferences)
        source->request_stop();
    }
  } cut;
  cut.source = &source;
  world.shared.documentBuilder->addBuildPhaseListener(cut);
  bool cancelled = false;
  try {
    world.shared.documentBuilder->build(set, cursiv::BuildOptions{false, {}},
                                        source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  world.shared.documentBuilder->removeBuildPhaseListener(cut);
  CHECK(cancelled);
  CHECK(a->state.load() == cursiv::DocumentState::IndexedReferences);

  world.shared.documentBuilder->update({}, {});
  // Finished, so the next ask takes.
  world.shared.documentBuilder->build(set);
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
}

// Per document, not per phase: each is announced the moment it arrives, which
// is what lets a server publish one file's diagnostics without waiting for the
// workspace. Fired from the worker that carried it, hence the lock.
class HeardPerDocument final : public cursiv::DocumentPhaseListener {
 public:
  void onDocumentPhase(cursiv::Document& document,
                       cursiv::DocumentState reached,
                       const cursiv::CancellationToken&) override {
    const std::scoped_lock lock(_mutex);
    _rungs[document.id].push_back(reached);
  }

  [[nodiscard]] std::vector<cursiv::DocumentState> of(
      cursiv::DocumentId id) const {
    const std::scoped_lock lock(_mutex);
    const auto at = _rungs.find(id);
    return at == _rungs.end() ? std::vector<cursiv::DocumentState>{}
                              : at->second;
  }

 private:
  mutable std::mutex _mutex;
  std::unordered_map<cursiv::DocumentId, std::vector<cursiv::DocumentState>>
      _rungs;
};

const cursiv::AstNode* referenced_type(const cursiv::Document& document) {
  const auto* root = document.parseResult.value_as<ws::ast::Workspace>();
  if (root == nullptr || root->elements.empty()) return nullptr;
  const auto* entity = cursiv::ast_cast<ws::ast::Entity>(root->elements[0]);
  if (entity == nullptr || entity->features.empty()) return nullptr;
  return entity->features[0]->type.get();
}

struct CaseInsensitiveScope final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;

 protected:
  bool isCaseSensitive(const cursiv::ReferenceInfo&) const noexcept override {
    return false;
  }
};

void a_new_homonym_retargets_without_reparsing(bool fromDisk,
                                             bool caseInsensitive) {
  World world([&](cursiv::CoreServices& language) {
    if (caseInsensitive)
      language.scopeProvider = std::make_unique<CaseInsensitiveScope>(language);
  });
  auto files = std::make_unique<cursiv::MemoryFileSystemProvider>();
  auto* disk = files.get();
  world.shared.fileSystemProvider = std::move(files);
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  auto* b = world.add("file:///b.ws", "datatype Same");
  auto* c = world.add("file:///c.ws", caseInsensitive ? "entity C { f : sAmE }"
                                                     : "entity C { f : Same }");
  cursiv::Document* initial[] = {b, c};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(initial, {}, token);
  });
  CHECK(referenced_type(*c) != nullptr &&
        referenced_type(*c)->getDocument() == b);
  const auto* root = c->parseResult.value;
  const auto revision = c->buildRevision;

  HeardPerDocument heard;
  builder.addDocumentPhaseListener(heard);
  const auto add = [&](const std::string& uri, const std::string& text) {
    if (fromDisk) disk->addFile(uri, text);
    lock.write([&](const cursiv::CancellationToken& token) {
      if (fromDisk) {
        const std::string_view changed[] = {uri};
        builder.update(changed, {}, token);
      } else {
        cursiv::Document* added[] = {world.add(uri, text)};
        builder.build(added, {}, token);
      }
    });
    return world.shared.documents->getDocument(uri);
  };
  const auto checkTarget = [&](const cursiv::Document* expected) {
    CHECK(c->parseResult.value == root);
    CHECK(c->buildRevision == revision);
    CHECK(c->state.load() == cursiv::DocumentState::Validated);
    CHECK(c->diagnostics.empty());
    const auto* target = referenced_type(*c);
    CHECK(target != nullptr && target->getDocument() == expected);
  };
  const std::string declaration =
      caseInsensitive ? "datatype SAME" : "datatype Same";
  const auto a = add("file:///a.ws", declaration);
  CHECK(a != nullptr && b->id < a->id);
  checkTarget(a.get());
  CHECK(std::ranges::count(heard.of(c->id), cursiv::DocumentState::Linked) == 1);
  CHECK(std::ranges::count(heard.of(c->id), cursiv::DocumentState::Validated) == 1);

  const auto rungs = heard.of(c->id);
  const auto* unchangedTarget = referenced_type(*c);
  (void)add("file:///aa.ws", "datatype Other");
  checkTarget(a.get());
  CHECK(referenced_type(*c) == unchangedTarget);
  CHECK(heard.of(c->id) == rungs);

  lock.write([&](const cursiv::CancellationToken& token) {
    const std::string_view deleted[] = {"file:///a.ws"};
    builder.update({}, deleted, token);
  });
  CHECK(world.shared.documents->getDocument("file:///a.ws") == nullptr);
  checkTarget(b);
  CHECK(std::ranges::count(heard.of(c->id), cursiv::DocumentState::Linked) == 2);

  const auto recreated = add("file:///a.ws", declaration);
  CHECK(recreated != nullptr && recreated != a);
  checkTarget(recreated.get());
  CHECK(std::ranges::count(heard.of(c->id), cursiv::DocumentState::Linked) == 3);
  CHECK(std::ranges::count(heard.of(c->id), cursiv::DocumentState::Validated) == 3);
  builder.removeDocumentPhaseListener(heard);
}

void cancelling_after_an_export_does_not_leave_stale_links(bool fromDisk) {
  cursiv::CancellationTokenSource cancellation;
  World world;
  struct CancelAfterExport final : cursiv::DefaultIndexManager {
    using DefaultIndexManager::DefaultIndexManager;
    cursiv::CancellationTokenSource* cancellation = nullptr;
    std::atomic<bool> fired{false};
    void updateContent(const cursiv::Document& document,
                       const cursiv::CancellationToken& token) override {
      DefaultIndexManager::updateContent(document, token);
      if (document.uri == "file:///a.ws" && !fired.exchange(true))
        cancellation->request_stop();
    }
  };
  auto index = std::make_unique<CancelAfterExport>(world.shared);
  index->cancellation = &cancellation;
  const auto* observed = index.get();
  world.shared.indexManager = std::move(index);
  auto files = std::make_unique<cursiv::MemoryFileSystemProvider>();
  files->addFile("file:///a.ws", "datatype Same");
  world.shared.fileSystemProvider = std::move(files);
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  auto* b = world.add("file:///b.ws", "datatype Same");
  auto* c = world.add("file:///c.ws", "entity C { f : Same }");
  cursiv::Document* initial[] = {b, c};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(initial, {}, token);
  });
  CHECK(referenced_type(*c) != nullptr &&
        referenced_type(*c)->getDocument() == b);
  const auto* root = c->parseResult.value;
  const auto revision = c->buildRevision;
  bool cancelled = false;
  lock.write([&](const cursiv::CancellationToken&) {
    try {
      if (fromDisk) {
        const std::string_view changed[] = {"file:///a.ws"};
        builder.update(changed, {}, cancellation.get_token());
      } else {
        cursiv::Document* added[] = {world.add("file:///a.ws", "datatype Same")};
        builder.build(added, {}, cancellation.get_token());
      }
    } catch (const cursiv::OperationCancelled&) {
      cancelled = true;
    }
  });
  CHECK(cancelled && observed->fired);
  const auto a = world.shared.documents->getDocument("file:///a.ws");
  CHECK(a != nullptr);
  const auto* exported = world.shared.indexManager->findByName(
      "Same", true, {}, *b->language->astReflection);
  CHECK(exported != nullptr && a != nullptr && exported->documentId == a->id);
  CHECK(c->parseResult.value == root && c->buildRevision == revision);
  if (c->state.load() == cursiv::DocumentState::Validated) {
    const auto* target = referenced_type(*c);
    CHECK(target != nullptr && target->getDocument() == a.get());
  }

  lock.write([&](const cursiv::CancellationToken& token) {
    builder.update({}, {}, token);
  });
  CHECK(c->parseResult.value == root && c->buildRevision == revision);
  CHECK(c->state.load() == cursiv::DocumentState::Validated);
  CHECK(!c->hasLinkingErrors() && c->diagnostics.empty());
  CHECK(referenced_type(*c) != nullptr &&
        referenced_type(*c)->getDocument() == a.get());
}

void deferred_callbacks_are_drained_before_failure() {
  World world;
  auto files = std::make_unique<cursiv::MemoryFileSystemProvider>();
  files->addFile("file:///a.ws", "datatype Same");
  world.shared.fileSystemProvider = std::move(files);
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  auto* b = world.add("file:///b.ws", "datatype Same");
  auto* c = world.add("file:///c.ws", "entity C { f : Same }");
  cursiv::Document* initial[] = {b, c};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(initial, {}, token);
  });
  const auto* root = c->parseResult.value;
  const auto revision = c->buildRevision;
  struct ThrowOnParsed final : cursiv::DocumentPhaseListener {
    std::atomic<unsigned> parsed{0}, indexed{0};
    std::atomic<bool> thrown{false};
    void onDocumentPhase(cursiv::Document& document, cursiv::DocumentState state,
                         const cursiv::CancellationToken&) override {
      if (document.uri != "file:///a.ws") return;
      if (state == cursiv::DocumentState::Parsed) {
        ++parsed;
        if (!thrown.exchange(true)) throw std::runtime_error("listener");
      } else if (state == cursiv::DocumentState::IndexedContent) {
        ++indexed;
      }
    }
  } listener;
  builder.addDocumentPhaseListener(listener);
  bool failed = false;
  try {
    lock.write([&](const cursiv::CancellationToken& token) {
      const std::string_view changed[] = {"file:///a.ws"};
      builder.update(changed, {}, token);
    });
  } catch (const std::runtime_error& error) {
    failed = std::string_view(error.what()) == "listener";
  }
  CHECK(failed);
  CHECK(listener.parsed == 1 && listener.indexed == 1);
  const auto a = world.shared.documents->getDocument("file:///a.ws");
  CHECK(a != nullptr && a->state.load() == cursiv::DocumentState::IndexedContent);
  CHECK(c->parseResult.value == root && c->buildRevision == revision);
  CHECK(c->state.load() < cursiv::DocumentState::Linked);

  lock.write([&](const cursiv::CancellationToken& token) {
    builder.update({}, {}, token);
  });
  builder.removeDocumentPhaseListener(listener);
  CHECK(listener.parsed == 1 && listener.indexed == 1);
  CHECK(c->parseResult.value == root && c->buildRevision == revision);
  CHECK(c->state.load() == cursiv::DocumentState::Validated);
  CHECK(c->diagnostics.empty());
  CHECK(referenced_type(*c) != nullptr &&
        referenced_type(*c)->getDocument() == a.get());
}

#if !defined(__wasi__) || defined(_REENTRANT)
void phase_callbacks_read_after_homonym_invalidation() {
  World world;
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(2);
  auto files = std::make_unique<cursiv::MemoryFileSystemProvider>();
  files->addFile("file:///a.ws", "datatype Same");
  files->addFile("file:///other.ws", "datatype Other");
  world.shared.fileSystemProvider = std::move(files);
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  auto* b = world.add("file:///b.ws", "datatype Same");
  auto* c = world.add("file:///c.ws", "entity C { f : Same }");
  cursiv::Document* initial[] = {b, c};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(initial, {}, token);
  });
  const auto* root = c->parseResult.value;
  const auto revision = c->buildRevision;
  struct ReadFromPhase final : cursiv::DocumentPhaseListener {
    cursiv::WorkspaceLock* lock = nullptr;
    const cursiv::Document* dependent = nullptr;
    const cursiv::AstNode* root = nullptr;
    std::uint64_t revision = 0;
    std::atomic<unsigned> parsed{0}, indexed{0}, admitted{0}, invalidated{0};
    std::atomic<unsigned> wrong{0};
    void onDocumentPhase(cursiv::Document& document, cursiv::DocumentState state,
                         const cursiv::CancellationToken&) override {
      if (document.uri != "file:///a.ws") return;
      if (state == cursiv::DocumentState::Parsed)
        ++parsed;
      else if (state == cursiv::DocumentState::IndexedContent)
        ++indexed;
      else
        return;
      cursiv::CancellationTokenSource timeout;
      std::promise<void> completed;
      auto done = completed.get_future();
      std::jthread watchdog([&] {
        if (done.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
          timeout.request_stop();
      });
      const bool read = lock->read(
          [&] {
            ++admitted;
            if (dependent->state.load() < cursiv::DocumentState::Linked)
              ++invalidated;
            if (!lock->settled() || dependent->parseResult.value != root ||
                dependent->buildRevision != revision)
              ++wrong;
          },
          timeout.get_token(), cursiv::WorkspaceLock::ReadPriority::Immediate);
      completed.set_value();
      if (!read) ++wrong;
    }
  } reader;
  reader.lock = &lock;
  reader.dependent = c;
  reader.root = root;
  reader.revision = revision;
  builder.addDocumentPhaseListener(reader);
  lock.write([&](const cursiv::CancellationToken& token) {
    const std::string_view changed[] = {"file:///a.ws", "file:///other.ws"};
    builder.update(changed, {}, token);
  });
  builder.removeDocumentPhaseListener(reader);
  CHECK(reader.parsed == 1 && reader.indexed == 1);
  CHECK(reader.admitted == 2 && reader.invalidated == 2 && reader.wrong == 0);
  CHECK(c->state.load() == cursiv::DocumentState::Validated);
  const auto a = world.shared.documents->getDocument("file:///a.ws");
  CHECK(a != nullptr && referenced_type(*c) != nullptr &&
        referenced_type(*c)->getDocument() == a.get());
}
#endif

void every_document_is_announced_on_its_own() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype String\n");
  cursiv::Document* const b = world.add("file:///b.ws", "datatype Other\n");
  cursiv::Document* const set[] = {a, b};

  HeardPerDocument heard;
  world.shared.documentBuilder->addDocumentPhaseListener(heard);
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeDocumentPhaseListener(heard);

  const std::vector<cursiv::DocumentState> forA = heard.of(a->id);
  const std::vector<cursiv::DocumentState> forB = heard.of(b->id);
  // Both climbed the same ladder, each announced for itself.
  CHECK(!forA.empty());
  CHECK(forA == forB);
  CHECK(forA.back() == cursiv::DocumentState::Validated);
  // ...and the top rung is announced once, not once per document in the build.
  CHECK(std::ranges::count(forA, cursiv::DocumentState::Validated) == 1);
}

// The rungs of the whole build in the order the workers reached them, the
// document that reached each alongside. Written from the pool, hence the lock.
class HeardSequence final : public cursiv::DocumentPhaseListener {
 public:
  struct Step {
    cursiv::DocumentId id;
    cursiv::DocumentState reached;
  };

  void onDocumentPhase(cursiv::Document& document,
                       cursiv::DocumentState reached,
                       const cursiv::CancellationToken&) override {
    const std::scoped_lock lock(_mutex);
    _steps.push_back({document.id, reached});
  }

  [[nodiscard]] std::vector<Step> steps() const {
    const std::scoped_lock lock(_mutex);
    return _steps;
  }

 private:
  mutable std::mutex _mutex;
  std::vector<Step> _steps;
};

// The barrier, read off the sequence: every local scope is written before the
// first link. A scope provider may hunt cross-document, so a link overlapping a
// document still being scoped would memoise a false "not found" -- and a
// different one on each run. The property holds whatever the pool interleaves.
void the_scopes_are_barred_before_the_first_link() {
  const World world;
  std::vector<cursiv::Document*> set;
  // Enough documents for the pool to overlap them: on one, the order would
  // hold with no barrier at all.
  for (const std::string_view name : {"a", "b", "c", "d", "e", "f", "g", "h"})
    set.push_back(world.add("file:///" + std::string(name) + ".ws",
                            "datatype T" + std::string(name) +
                                "\nentity E { f : T" + std::string(name) +
                                " }\n"));

  HeardSequence heard;
  world.shared.documentBuilder->addDocumentPhaseListener(heard);
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeDocumentPhaseListener(heard);

  const std::vector<HeardSequence::Step> steps = heard.steps();
  const auto firstLink = std::ranges::find(steps, cursiv::DocumentState::Linked,
                                           &HeardSequence::Step::reached);
  CHECK(firstLink != steps.end());
  // Every document scoped before that one, and none after it.
  std::unordered_set<cursiv::DocumentId> scoped;
  for (auto at = steps.begin(); at != firstLink; ++at)
    if (at->reached == cursiv::DocumentState::ComputedScopes)
      scoped.insert(at->id);
  CHECK(scoped.size() == set.size());
  CHECK(
      std::none_of(firstLink, steps.end(), [](const HeardSequence::Step& step) {
        return step.reached == cursiv::DocumentState::ComputedScopes;
      }));
}

// A tree with no disk under it: one folder holding two sources. Enough for
// `findChangedUris` to see a directory and for the walk to enumerate it.
class FakeFiles final : public cursiv::FileSystemProvider {
 public:
  [[nodiscard]] cursiv::FileSystemNode stat(
      std::string_view uri) const override {
    if (uri == kFolder) return {false, true, std::string(uri)};
    if (uri == kA || uri == kB) return {true, false, std::string(uri)};
    throw cursiv::FileSystemError("nothing there");
  }
  [[nodiscard]] bool exists(std::string_view uri) const override {
    return uri == kFolder || uri == kA || uri == kB;
  }
  [[nodiscard]] std::vector<cursiv::FileSystemNode> readDirectory(
      std::string_view uri) const override {
    if (uri != kFolder) throw cursiv::FileSystemError("not a folder");
    return {{true, false, std::string(kA)}, {true, false, std::string(kB)}};
  }
  [[nodiscard]] std::string readFile(std::string_view) const override {
    return "datatype String\n";
  }
  [[nodiscard]] std::vector<std::uint8_t> readBinary(
      std::string_view) const override {
    return {};
  }

  static constexpr std::string_view kFolder = "file:///dir";
  static constexpr std::string_view kA = "file:///dir/a.ws";
  static constexpr std::string_view kB = "file:///dir/b.ws";
};

void a_new_file_and_a_recreated_file_are_parsed_once() {
  CountingParser* counter = nullptr;
  World world([&](cursiv::CoreServices& language) {
    auto parser = std::make_unique<CountingParser>(std::move(language.parser));
    counter = parser.get();
    language.parser = std::move(parser);
  });
  world.shared.fileSystemProvider = std::make_unique<FakeFiles>();
  const std::string_view changed[] = {FakeFiles::kA};
  world.shared.documentBuilder->update(changed, {});
  const auto first = world.shared.documents->getDocument(FakeFiles::kA);
  CHECK(first != nullptr);
  CHECK(counter->calls.load() == 1);
  if (first == nullptr) return;
  CHECK(first->state.load() == cursiv::DocumentState::Validated);

  world.shared.documentBuilder->update(changed, changed);
  const auto recreated = world.shared.documents->getDocument(FakeFiles::kA);
  CHECK(recreated != nullptr);
  CHECK(counter->calls.load() == 2);
  if (recreated == nullptr) return;
  CHECK(recreated != first);
  CHECK(recreated->id == first->id);
  CHECK(recreated->state.load() == cursiv::DocumentState::Validated);
}

// A changed folder is every document under it. A checkout, a bulk copy, or a
// watcher that coalesced its events reports the directory -- and without the
// expansion nothing under it would be rebuilt.
void a_changed_folder_takes_its_documents_down() {
  World world;
  world.shared.fileSystemProvider = std::make_unique<FakeFiles>();
  cursiv::Document* const a =
      world.add(std::string(FakeFiles::kA), "datatype String\n");
  cursiv::Document* const b =
      world.add(std::string(FakeFiles::kB), "datatype Other\n");
  cursiv::Document* const set[] = {a, b};
  world.shared.documentBuilder->build(set);
  CHECK(a->state.load() == cursiv::DocumentState::Validated);

  const std::uint64_t beforeA = a->buildRevision;
  const std::uint64_t beforeB = b->buildRevision;
  const std::string_view changed[] = {FakeFiles::kFolder};
  world.shared.documentBuilder->update(changed, {});

  // Both went back down and climbed again; neither was left behind.
  CHECK(a->buildRevision != beforeA);
  CHECK(b->buildRevision != beforeB);
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(b->state.load() == cursiv::DocumentState::Validated);
}

// An unfinished request survives: a build interrupted before honouring its
// ask keeps that ask through the next `build`, and only a build that finished
// lets a later one replace it.
void an_interrupted_request_is_not_replaced() {
  const World world;
  cursiv::Document* const a =
      world.add("file:///a.ws", "entity E { f : Missing }\n");
  cursiv::Document* const set[] = {a};

  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool cancelled = false;
  try {
    world.shared.documentBuilder->build(set, cursiv::BuildOptions{false, {}},
                                        source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  CHECK(a->state.load() < cursiv::DocumentState::Validated);

  // The default ask (validating) does not replace the interrupted one.
  world.shared.documentBuilder->build(set);
  CHECK(a->state.load() == cursiv::DocumentState::IndexedReferences);
  CHECK(a->diagnostics.empty());  // the original no-validation ask held

  // Honoured now, so the next ask takes: the witness that the guard is about
  // completion, not a one-way door.
  world.shared.documentBuilder->build(set);
  CHECK(!a->diagnostics.empty());
}

// A caller's duplicate is one document, not two parallel bodies writing one
// state.
void a_duplicate_in_the_set_builds_once() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a, a, a};

  Heard heard;
  world.shared.documentBuilder->addBuildPhaseListener(heard);
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeBuildPhaseListener(heard);

  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(!heard.counts.empty());
  for (const std::size_t count : heard.counts) CHECK(count <= 1);
}

// The four answers of a wait: reached now, unreachable for a document that is
// not there, cancelled before anything.
void a_wait_answers_all_its_outcomes() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  world.shared.documentBuilder->build(set);

  const cursiv::DocumentBuilder& builder = *world.shared.documentBuilder;
  CHECK(builder.waitUntil(cursiv::DocumentState::Validated) ==
        cursiv::WaitOutcome::Reached);
  CHECK(builder.waitUntil(cursiv::DocumentState::Validated, a->id) ==
        cursiv::WaitOutcome::Reached);
  CHECK(builder.waitUntil(cursiv::DocumentState::Validated,
                          static_cast<cursiv::DocumentId>(9999)) ==
        cursiv::WaitOutcome::Unreachable);
  cursiv::CancellationTokenSource source;
  source.request_stop();
  CHECK(builder.waitUntil(cursiv::DocumentState::Validated, a->id,
                          source.get_token()) ==
        cursiv::WaitOutcome::Cancelled);
}

void a_wait_can_precede_the_first_build() {
  using namespace std::chrono_literals;
  const World world;
  auto* const document = world.add("file:///first.ws", "datatype A\n");
  const auto& builder = *world.shared.documentBuilder;
  cursiv::CancellationTokenSource stop;
  auto workspace = std::async(std::launch::async, [&] {
    return builder.waitUntil(cursiv::DocumentState::Validated,
                             stop.get_token());
  });
  auto individual = std::async(std::launch::async, [&] {
    return builder.waitUntil(cursiv::DocumentState::Validated, document->id,
                             stop.get_token());
  });
  CHECK(workspace.wait_for(20ms) == std::future_status::timeout);
  CHECK(individual.wait_for(20ms) == std::future_status::timeout);
  cursiv::Document* const documents[] = {document};
  builder.build(documents);
  const auto ready = std::future_status::ready;
  const bool completed = workspace.wait_for(2s) == ready &&
                         individual.wait_for(2s) == ready;
  CHECK(completed);
  if (!completed) stop.request_stop();
  CHECK(workspace.get() == cursiv::WaitOutcome::Reached);
  CHECK(individual.get() == cursiv::WaitOutcome::Reached);
}

// The flag a rebuild rides when a missing symbol turns up elsewhere.
void a_document_knows_its_links_are_broken() {
  const World world;
  cursiv::Document* const broken =
      world.add("file:///a.ws", "entity E { f : Missing }\n");
  cursiv::Document* const clean = world.add("file:///b.ws", "datatype B\n");
  cursiv::Document* const set[] = {broken, clean};
  world.shared.documentBuilder->build(set);
  CHECK(broken->hasLinkingErrors());
  CHECK(!clean->hasLinkingErrors());
}

// A build that fails -- a phase listener throwing is one -- answers its
// waiters with the failure, not with silence.
void a_failed_build_answers_its_waiters() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};

  struct Throwing : cursiv::DocumentPhaseListener {
    void onDocumentPhase(cursiv::Document&, cursiv::DocumentState,
                         const cursiv::CancellationToken&) override {
      throw std::runtime_error("phase listener failed");
    }
  } throwing;
  world.shared.documentBuilder->addDocumentPhaseListener(throwing);
  bool failed = false;
  try {
    world.shared.documentBuilder->build(set);
  } catch (const std::runtime_error&) {
    failed = true;
  }
  world.shared.documentBuilder->removeDocumentPhaseListener(throwing);
  CHECK(failed);
  CHECK(world.shared.documentBuilder->waitUntil(
            cursiv::DocumentState::Validated, a->id) ==
        cursiv::WaitOutcome::BuildFailed);
}

// A call cut short before its phases fails like one cut inside them: an
// update listener throwing is one. A wait begun before the call and one begun
// after it both learn that the rung will not come. A listener that cancels is
// no failure: the wait is for the build that supersedes it.
void a_failed_update_listener_answers_the_waiters(bool update) {
  using namespace std::chrono_literals;
  const World world;
  cursiv::DocumentBuilder& builder = *world.shared.documentBuilder;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  const std::string_view changed[] = {"file:///a.ws"};
  const auto run = [&] {
    if (update)
      builder.update(changed, {});
    else
      builder.build(set);
  };
  cursiv::CancellationTokenSource stop;
  std::vector<std::future<cursiv::WaitOutcome>> waits;
  const auto wait = [&] {
    waits.push_back(std::async(std::launch::async, [&] {
      return builder.waitUntil(cursiv::DocumentState::Validated,
                               stop.get_token());
    }));
    waits.push_back(std::async(std::launch::async, [&] {
      return builder.waitUntil(cursiv::DocumentState::Validated, a->id,
                               stop.get_token());
    }));
  };
  const auto answers = [&](cursiv::WaitOutcome expected) {
    for (std::future<cursiv::WaitOutcome>& waiting : waits) {
      const bool answered = waiting.wait_for(2s) == std::future_status::ready;
      CHECK(answered);
      if (!answered) stop.request_stop();
      CHECK(waiting.get() == expected);
    }
    waits.clear();
  };

  wait();
  for (const std::future<cursiv::WaitOutcome>& waiting : waits)
    CHECK(waiting.wait_for(20ms) == std::future_status::timeout);
  struct Throwing final : cursiv::DocumentUpdateListener {
    void onUpdate(std::span<const std::string_view>,
                  std::span<const std::string_view>) override {
      throw std::logic_error("listener");
    }
  } throwing;
  builder.addDocumentUpdateListener(throwing);
  bool failed = false;
  try {
    run();
  } catch (const std::logic_error&) {
    failed = true;
  }
  builder.removeDocumentUpdateListener(throwing);
  CHECK(failed);
  wait();
  answers(cursiv::WaitOutcome::BuildFailed);

  struct Cancelling final : cursiv::DocumentUpdateListener {
    void onUpdate(std::span<const std::string_view>,
                  std::span<const std::string_view>) override {
      throw cursiv::OperationCancelled();
    }
  } cancelling;
  builder.addDocumentUpdateListener(cancelling);
  bool cancelled = false;
  try {
    run();
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  builder.removeDocumentUpdateListener(cancelling);
  CHECK(cancelled);
  wait();
  for (const std::future<cursiv::WaitOutcome>& waiting : waits)
    CHECK(waiting.wait_for(20ms) == std::future_status::timeout);
  run();
  answers(cursiv::WaitOutcome::Reached);
}

// The pattern `routeDocument` runs, against a build that never stops: wait
// for the rung, be admitted beside the settled write, ask the rung again,
// then touch the tree. Its value is under TSan: drop the second ask and the
// sanitizer reports a data race between the reader and the parse phase
// assigning `ParseResult::cst`. Without a sanitizer, `torn` still counts a
// reader handed a tree that says `Parsed` while holding nothing.
//
// The second ask is not redundant with the first: `waitUntil` answered
// before the admission, and a write may have entered and dropped this
// document in between. It is what keeps `torn` at zero.
void a_request_reads_beside_a_running_build() {
  const World world;
  const std::string uri = "file:///live.ws";
  cursiv::Document* const document =
      world.add(uri, "datatype String\nentity E { f : String }\n");
  cursiv::WorkspaceLock& lock = *world.shared.workspaceLock;
  const cursiv::DocumentBuilder& builder = *world.shared.documentBuilder;
  const cursiv::DocumentId id = world.shared.documents->getDocumentId(uri);

  // One build first: the race is about a document that has a tree to lose.
  cursiv::Document* const set[] = {document};
  builder.build(set);
  CHECK(document->state.load() == cursiv::DocumentState::Validated);

  std::atomic<bool> stop{false};
  std::atomic<std::size_t> served{0};
  std::atomic<std::size_t> torn{0};

  std::jthread writer([&] {
    const std::string_view moved[] = {std::string_view(uri)};
    while (!stop.load())
      lock.write([&](const cursiv::CancellationToken& token) {
        builder.update(moved, {}, token);
      });
  });

  for (int round = 0; round < 400; ++round) {
    // A rung that is not reached is the writer mid-reset: ask again.
    if (builder.waitUntil(cursiv::DocumentState::Parsed, id, {}) !=
        cursiv::WaitOutcome::Reached)
      continue;
    (void)lock.read(
        [&] {
          const std::shared_ptr<cursiv::Document> d =
              world.shared.documents->getDocument(uri);
          if (d == nullptr) return;
          // The second ask, which is the point: `waitUntil` answered before
          // this admission, and a write may have dropped the document since.
          if (d->state.load() < cursiv::DocumentState::Parsed) return;
          const cursiv::Cst& cst = d->parseResult.cst();
          // A tree that says `Parsed` and holds nothing is what a reader let
          // into the mutation would see.
          if (cst.size() == 0 || cst.text(cst.entry_root()).empty()) ++torn;
          ++served;
        },
        {}, cursiv::WorkspaceLock::ReadPriority::Immediate);
  }
  stop = true;
  writer.join();

  CHECK(torn.load() == 0);
  CHECK(served.load() > 0);
}

// The other half of the race the CST lane above does not reach: two linked
// documents, and a reader that goes through the index instead of one
// document's own tree.
void a_request_reads_the_index_beside_a_running_build() {
  const World world;
  cursiv::Document* const a =
      world.add("file:///shared.ws", "datatype Shared\n");
  cursiv::Document* const b =
      world.add("file:///user.ws", "entity E { f : Shared }\n");
  cursiv::WorkspaceLock& lock = *world.shared.workspaceLock;
  const cursiv::DocumentBuilder& builder = *world.shared.documentBuilder;
  cursiv::IndexManager& index = *world.shared.indexManager;

  cursiv::Document* const set[] = {a, b};
  builder.build(set);
  CHECK(b->state.load() == cursiv::DocumentState::Validated);

  std::atomic<bool> stop{false};
  std::atomic<std::size_t> served{0};
  std::atomic<std::size_t> torn{0};

  std::jthread writer([&] {
    const std::string_view changed[] = {"file:///shared.ws"};
    while (!stop.load())
      lock.write([&](const cursiv::CancellationToken& token) {
        builder.update(changed, {}, token);
      });
  });

  for (int round = 0; round < 400; ++round) {
    (void)lock.read(
        [&] {
          (void)index.visitElements(nullptr, {},
                                    [&](const cursiv::AstNodeDescription& d) {
                                      if (d.name.empty()) ++torn;
                                      return true;
                                    });
          ++served;
        },
        {}, cursiv::WorkspaceLock::ReadPriority::Immediate);
  }
  stop = true;
  writer.join();

  CHECK(torn.load() == 0);
  CHECK(served.load() > 0);
}

// Cuts the build from inside one service call, once: the cancellation lands
// where a body has done half its work.
class CancelOnceReferences final : public cursiv::ReferenceDescriptionProvider {
 public:
  CancelOnceReferences(const cursiv::CoreServices& services,
                       cursiv::CancellationTokenSource* source)
      : _inner(services), _source(source) {}
  [[nodiscard]] std::vector<cursiv::ReferenceDescription> createDescriptions(
      const cursiv::Document& document,
      const cursiv::CancellationToken& token) const override {
    if (!_fired.exchange(true)) {
      _source->request_stop();
      throw cursiv::OperationCancelled();
    }
    return _inner.createDescriptions(document, token);
  }

 private:
  cursiv::DefaultReferenceDescriptionProvider _inner;
  cursiv::CancellationTokenSource* _source;
  mutable std::atomic<bool> _fired{false};
};

class CancelOnceScopes final : public cursiv::ScopeComputation {
 public:
  CancelOnceScopes(const cursiv::CoreServices& services,
                   cursiv::CancellationTokenSource* source)
      : _inner(services), _source(source) {}
  [[nodiscard]] std::vector<cursiv::AstNodeDescription> collectExportedSymbols(
      const cursiv::Document& document,
      const cursiv::CancellationToken& token) const override {
    if (!_fired.exchange(true)) {
      _source->request_stop();
      throw cursiv::OperationCancelled();
    }
    return _inner.collectExportedSymbols(document, token);
  }
  [[nodiscard]] cursiv::LocalSymbols collectLocalSymbols(
      const cursiv::Document& document,
      const cursiv::CancellationToken& token) const override {
    return _inner.collectLocalSymbols(document, token);
  }

 private:
  cursiv::DefaultScopeComputation _inner;
  cursiv::CancellationTokenSource* _source;
  mutable std::atomic<bool> _fired{false};
};

// Linked, then cut before its references were indexed: a rung the document
// cannot keep, or the next build would skip the link and index nothing.
void a_build_cut_after_linking_relinks_next_time() {
  cursiv::CancellationTokenSource source;
  const World world([&](cursiv::CoreServices& language) {
    language.referenceDescriptionProvider =
        std::make_unique<CancelOnceReferences>(language, &source);
  });
  cursiv::Document* const a =
      world.add("file:///a.ws", "entity E { f : Missing }\n");
  cursiv::Document* const set[] = {a};

  bool cancelled = false;
  try {
    world.shared.documentBuilder->build(set, {}, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  CHECK(a->state.load() < cursiv::DocumentState::Linked);

  world.shared.documentBuilder->build(set);
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(a->hasLinkingErrors());
  CHECK(!a->diagnostics.empty());
}

void a_reset_to_computed_scopes_keeps_lazy_resolution() {
  World world;
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  auto* target = world.add("file:///target.ws", "datatype T");
  auto* consumer = world.add("file:///use.ws", "entity E { f : T }");
  cursiv::Document* documents[] = {target, consumer};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(documents, {}, token);
  });
  const auto* original = referenced_type(*consumer);
  CHECK(original != nullptr);
  lock.write([&](const cursiv::CancellationToken&) {
    builder.resetToState(*consumer, cursiv::DocumentState::ComputedScopes);
  });
  CHECK(consumer->state.load() == cursiv::DocumentState::ComputedScopes);
  (void)lock.read([&] { CHECK(referenced_type(*consumer) == original); });
}

void a_build_from_linked_reindexes_its_resolutions(bool changedTarget) {
  World world;
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  auto* target = world.add("file:///target.ws", "datatype T");
  auto* consumer = world.add("file:///use.ws", "entity E { f : T }");
  cursiv::Document* documents[] = {target, consumer};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(documents, {}, token);
  });
  const auto* original = referenced_type(*consumer);
  CHECK(original != nullptr);
  if (original == nullptr) return;
  const auto targetSymbol = original->cstNodeId;
  lock.write([&](const cursiv::CancellationToken&) {
    builder.resetToState(*consumer, cursiv::DocumentState::Linked);
  });
  CHECK(consumer->state.load() == cursiv::DocumentState::Linked);
  CHECK(referenced_type(*consumer) == original);
  if (changedTarget) {
    cursiv::CancellationTokenSource cancellation;
    cancellation.request_stop();
    const std::string_view changed[] = {"file:///target.ws"};
    lock.write([&](const cursiv::CancellationToken&) {
      builder.update(changed, {}, cancellation.get_token());
    });
    CHECK(consumer->state.load() == cursiv::DocumentState::ComputedScopes);
    lock.write([&](const cursiv::CancellationToken& token) {
      builder.update({}, {}, token);
    });
  }
  cursiv::Document* consumers[] = {consumer};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(consumers, {}, token);
  });
  std::size_t edges = 0;
  (void)world.shared.indexManager->visitReferences(
      target->id, targetSymbol, [&](const cursiv::ReferenceDescription&) {
        ++edges;
        return true;
      });
  CHECK(edges == 1);
  CHECK(consumer->state.load() == cursiv::DocumentState::Validated);
}

void an_update_drops_unindexed_resolutions(cursiv::DocumentState stopAfter,
                                           bool deleted) {
  World world;
  auto files = std::make_unique<cursiv::MemoryFileSystemProvider>();
  auto* disk = files.get();
  world.shared.fileSystemProvider = std::move(files);
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  auto* target = world.add("file:///target.ws", "datatype T");
  cursiv::Document* const targets[] = {target};
  lock.write([&](const cursiv::CancellationToken& token) {
    builder.build(targets, {}, token);
  });
  auto* consumer = world.add("file:///use.ws", "entity E { f : T }");
  cursiv::Document* const consumers[] = {consumer};
  const auto* root = consumer->parseResult.value;
  const auto revision = consumer->buildRevision;
  cursiv::CancellationTokenSource cancellation;
  struct Cut final : cursiv::DocumentPhaseListener {
    cursiv::DocumentId document;
    cursiv::DocumentState after;
    cursiv::CancellationTokenSource* source;
    void onDocumentPhase(cursiv::Document& reached, cursiv::DocumentState state,
                         const cursiv::CancellationToken&) override {
      if (reached.id == document && state == after) source->request_stop();
    }
  } cut;
  cut.document = consumer->id;
  cut.after = stopAfter;
  cut.source = &cancellation;
  builder.addDocumentPhaseListener(cut);
  bool cancelled = false;
  lock.write([&](const cursiv::CancellationToken&) {
    try {
      builder.build(consumers, {false, {}}, cancellation.get_token());
    } catch (const cursiv::OperationCancelled&) {
      cancelled = true;
    }
  });
  builder.removeDocumentPhaseListener(cut);
  CHECK(cancelled);
  CHECK(consumer->state.load() == cursiv::DocumentState::ComputedScopes);
  const cursiv::AstNode* original = nullptr;
  (void)lock.read([&] { original = referenced_type(*consumer); });
  CHECK(original != nullptr && original->getDocument() == target);
  CHECK(!consumer->hasLinkingErrors());

  lock.write([&](const cursiv::CancellationToken&) {
    try {
      builder.update({}, {}, cancellation.get_token());
    } catch (const cursiv::OperationCancelled&) {
    }
  });
  CHECK(referenced_type(*consumer) == original);

  cursiv::CancellationTokenSource updateCancellation;
  struct Updated final : cursiv::DocumentUpdateListener {
    const cursiv::Document* consumer;
    cursiv::CancellationTokenSource* source;
    bool cleared = false;
    void onUpdate(std::span<const std::string_view>,
                  std::span<const std::string_view>) override {
      cleared = referenced_type(*consumer) == nullptr;
      source->request_stop();
    }
  } updated;
  updated.consumer = consumer;
  updated.source = &updateCancellation;
  builder.addDocumentUpdateListener(updated);
  disk->addFile("file:///target.ws", "datatype Other");
  const std::string_view moved[] = {"file:///target.ws"};
  cancelled = false;
  lock.write([&](const cursiv::CancellationToken&) {
    try {
      if (deleted)
        builder.update({}, moved, updateCancellation.get_token());
      else
        builder.update(moved, {}, updateCancellation.get_token());
    } catch (const cursiv::OperationCancelled&) {
      cancelled = true;
    }
  });
  builder.removeDocumentUpdateListener(updated);
  CHECK(cancelled && updated.cleared);
  CHECK(consumer->parseResult.value == root);
  CHECK(consumer->buildRevision == revision);
  CHECK(consumer->state.load() == cursiv::DocumentState::ComputedScopes);
  (void)lock.read([&] { CHECK(referenced_type(*consumer) == nullptr); });
  CHECK(world.shared.documents->hasDocument("file:///target.ws") != deleted);

  lock.write([&](const cursiv::CancellationToken& token) {
    if (!deleted) builder.build(targets, {}, token);
    builder.build(consumers, {}, token);
  });
  CHECK(consumer->state.load() == cursiv::DocumentState::IndexedReferences);
  CHECK(consumer->hasLinkingErrors());
  CHECK(consumer->diagnostics.empty());
}

// An update cut short is cut before anything moved or after everything did:
// a subscriber told of the change finds the store already saying it.
void a_cut_update_leaves_no_half_reset() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const b = world.add("file:///b.ws", "entity E { f : A }\n");
  cursiv::Document* const c = world.add("file:///c.ws", "datatype C\n");
  cursiv::Document* const all[] = {a, b, c};
  world.shared.documentBuilder->build(all);
  CHECK(!b->hasLinkingErrors());

  struct Told final : cursiv::DocumentUpdateListener {
    const cursiv::Documents* store = nullptr;
    int calls = 0;
    bool deletedGone = false;
    bool changedDown = false;
    void onUpdate(std::span<const std::string_view> changed,
                  std::span<const std::string_view> deleted) override {
      ++calls;
      deletedGone = deleted.size() == 1 && !store->hasDocument(deleted[0]);
      const std::shared_ptr<cursiv::Document> d =
          changed.size() == 1 ? store->getDocument(changed[0]) : nullptr;
      changedDown =
          d != nullptr && d->state.load() == cursiv::DocumentState::Changed;
    }
  } told;
  told.store = world.shared.documents.get();
  world.shared.documentBuilder->addDocumentUpdateListener(told);

  cursiv::CancellationTokenSource source;
  source.request_stop();
  const std::string_view changed[] = {"file:///a.ws"};
  const std::string_view deleted[] = {"file:///c.ws"};
  bool cancelled = false;
  try {
    world.shared.documentBuilder->update(changed, deleted, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  world.shared.documentBuilder->removeDocumentUpdateListener(told);
  CHECK(cancelled);
  CHECK(told.calls == 1);
  CHECK(told.deletedGone);
  CHECK(told.changedDown);
  CHECK(a->state.load() == cursiv::DocumentState::Changed);
  CHECK(b->state.load() < cursiv::DocumentState::Validated);
  CHECK(!world.shared.documents->hasDocument("file:///c.ws"));
}

// An update cut before its build takes the workspace rung down with its
// documents: a wait on them is for the next build, not `Unreachable`.
void a_cut_update_takes_the_workspace_rung_down() {
  using namespace std::chrono_literals;
  const World world;
  cursiv::DocumentBuilder& builder = *world.shared.documentBuilder;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  builder.build(set);

  cursiv::CancellationTokenSource superseded;
  superseded.request_stop();
  const std::string_view changed[] = {"file:///a.ws"};
  try {
    builder.update(changed, {}, superseded.get_token());
  } catch (const cursiv::OperationCancelled&) {
  }
  CHECK(a->state.load() == cursiv::DocumentState::Changed);
  CHECK(builder.currentState() == cursiv::DocumentState::Changed);
  auto onA = std::async(std::launch::async, [&] {
    return builder.waitUntil(cursiv::DocumentState::Linked, a->id);
  });
  auto onAll = std::async(std::launch::async, [&] {
    return builder.waitUntil(cursiv::DocumentState::Linked);
  });
  CHECK(onA.wait_for(20ms) == std::future_status::timeout);
  CHECK(onAll.wait_for(0ms) == std::future_status::timeout);
  builder.update({}, {}, {});
  CHECK(onA.get() == cursiv::WaitOutcome::Reached);
  CHECK(onAll.get() == cursiv::WaitOutcome::Reached);

  // A build cut before its own phases does the same.
  struct Throwing final : cursiv::DocumentUpdateListener {
    void onUpdate(std::span<const std::string_view>,
                  std::span<const std::string_view>) override {
      throw std::runtime_error("listener");
    }
  } throwing;
  builder.addDocumentUpdateListener(throwing);
  try {
    builder.build(set);
  } catch (const std::runtime_error&) {
  }
  builder.removeDocumentUpdateListener(throwing);
  CHECK(builder.currentState() == cursiv::DocumentState::Changed);
}

// A document parsed by one attempt and indexed by the next crossed `Parsed`
// once, and hears it once.
void a_rung_crossed_once_is_announced_once() {
  cursiv::CancellationTokenSource source;
  const World world([&](cursiv::CoreServices& language) {
    language.scopeComputation =
        std::make_unique<CancelOnceScopes>(language, &source);
  });
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  world.shared.documentBuilder->resetToState(*a, cursiv::DocumentState::Changed);
  cursiv::Document* const set[] = {a};
  HeardPerDocument heard;
  world.shared.documentBuilder->addDocumentPhaseListener(heard);
  try {
    world.shared.documentBuilder->build(set, {}, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
  }
  CHECK(a->state.load() == cursiv::DocumentState::Parsed);
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeDocumentPhaseListener(heard);
  const std::vector<cursiv::DocumentState> rungs = heard.of(a->id);
  CHECK(std::ranges::count(rungs, cursiv::DocumentState::Parsed) == 1);
  CHECK(rungs.back() == cursiv::DocumentState::Validated);
}

// The same URI takes the same id back: an unfinished request of the dead
// document must not be what the reborn one is built with.
void a_deleted_documents_request_dies_with_it() {
  const World world;
  const std::string uri = "file:///a.ws";
  cursiv::Document* const a = world.add(uri, "entity E { f : Missing }\n");
  // Read before the delete: `a` is the store's, and the store frees it.
  const auto wasId = a->id;
  cursiv::Document* const set[] = {a};
  cursiv::CancellationTokenSource source;
  source.request_stop();
  try {
    world.shared.documentBuilder->build(set, cursiv::BuildOptions{false, {}},
                                        source.get_token());
  } catch (const cursiv::OperationCancelled&) {
  }
  const std::string_view gone[] = {std::string_view(uri)};
  world.shared.documentBuilder->update({}, gone);
  CHECK(!world.shared.documents->hasDocument(uri));

  cursiv::Document* const again = world.add(uri, "entity E { f : Missing }\n");
  CHECK(again->id == wasId);
  cursiv::Document* const reborn[] = {again};
  world.shared.documentBuilder->build(reborn);
  CHECK(!again->diagnostics.empty());
}

// Back to `Parsed` is back below the content index.
void a_reset_to_parsed_takes_the_exports_down() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  world.shared.documentBuilder->build(set);
  const auto exported = [&] {
    std::size_t count = 0;
    const cursiv::DocumentId only[] = {a->id};
    (void)world.shared.indexManager->visitElements(
        nullptr, only, [&](const cursiv::AstNodeDescription&) {
          ++count;
          return true;
        });
    return count;
  };
  CHECK(exported() > 0);
  world.shared.documentBuilder->resetToState(*a, cursiv::DocumentState::Parsed);
  CHECK(a->state.load() == cursiv::DocumentState::Parsed);
  CHECK(exported() == 0);
}

// A listener that gives up does not silence the one after it.
void every_listener_hears_a_rung() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  struct GivingUp final : cursiv::DocumentPhaseListener {
    void onDocumentPhase(cursiv::Document&, cursiv::DocumentState,
                         const cursiv::CancellationToken&) override {
      throw cursiv::OperationCancelled();
    }
  } givingUp;
  HeardPerDocument heard;
  world.shared.documentBuilder->addDocumentPhaseListener(givingUp);
  world.shared.documentBuilder->addDocumentPhaseListener(heard);
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeDocumentPhaseListener(heard);
  world.shared.documentBuilder->removeDocumentPhaseListener(givingUp);
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(heard.of(a->id).size() == cursiv::kDocumentStateCount - 2);
}

// A build says what it touches, the way an update does.
void a_build_announces_what_it_builds() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  struct Told final : cursiv::DocumentUpdateListener {
    std::vector<std::string> changed;
    std::size_t deleted = 0;
    void onUpdate(std::span<const std::string_view> c,
                  std::span<const std::string_view> d) override {
      changed.assign(c.begin(), c.end());
      deleted = d.size();
    }
  } told;
  world.shared.documentBuilder->addDocumentUpdateListener(told);
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeDocumentUpdateListener(told);
  CHECK(told.changed == std::vector<std::string>{"file:///a.ws"});
  CHECK(told.deleted == 0);
}

// Built without validation is done, not owing every category for ever.
void a_build_without_validation_is_complete() {
  const World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  HeardPerDocument phases;
  world.shared.documentBuilder->addDocumentPhaseListener(phases);
  world.shared.documentBuilder->build(set, cursiv::BuildOptions{false, {}});
  CHECK(a->state.load() == cursiv::DocumentState::IndexedReferences);
  CHECK(phases.of(a->id).back() == cursiv::DocumentState::IndexedReferences);
  CHECK(world.shared.documentBuilder->waitUntil(
            cursiv::DocumentState::Validated, a->id) ==
        cursiv::WaitOutcome::Unreachable);
  world.shared.documentBuilder->removeDocumentPhaseListener(phases);
  Heard heard;
  world.shared.documentBuilder->addBuildPhaseListener(heard);
  world.shared.documentBuilder->update({}, {});
  world.shared.documentBuilder->removeBuildPhaseListener(heard);
  CHECK(!heard.counts.empty());
  CHECK(heard.counts.back() == 0);
}

// Updates validate the built-in and fast categories by default; callers can
// override the update options.
void an_update_builds_with_the_update_options() {
  const World world;
  cursiv::DocumentBuilder& builder = *world.shared.documentBuilder;
  CHECK(builder.updateBuildOptions().validation);
  CHECK((builder.updateBuildOptions().validationCategories ==
         std::vector<const cursiv::ValidationCategory*>{&cursiv::built_in,
                                                        &cursiv::fast}));
  cursiv::Document* const a =
      world.add("file:///a.ws", "entity E { f : Missing }\n");
  const std::string_view changed[] = {"file:///a.ws"};
  builder.update(changed, {});
  CHECK(!a->diagnostics.empty());
  builder.updateBuildOptions() = cursiv::BuildOptions{false, {}};
  builder.update(changed, {});
  CHECK(a->state.load() == cursiv::DocumentState::IndexedReferences);
  CHECK(a->diagnostics.empty());
}

// A file the store does not hold waits unparsed for the parse phase: the
// write that installs it parses nothing, so the parse runs in parallel and
// under the token. A cancelled update leaves it unparsed; the next parses it.
void a_new_file_waits_unparsed_for_the_parse_phase() {
  CountingParser* counter = nullptr;
  World world([&](cursiv::CoreServices& language) {
    auto parser = std::make_unique<CountingParser>(std::move(language.parser));
    counter = parser.get();
    language.parser = std::move(parser);
  });
  world.shared.fileSystemProvider = std::make_unique<FakeFiles>();
  cursiv::CancellationTokenSource source;
  source.request_stop();
  const std::string_view changed[] = {FakeFiles::kA};
  bool cancelled = false;
  try {
    world.shared.documentBuilder->update(changed, {}, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  const auto installed = world.shared.documents->getDocument(FakeFiles::kA);
  CHECK(installed != nullptr);
  CHECK(counter->calls.load() == 0);
  if (installed == nullptr) return;
  CHECK(installed->state.load() == cursiv::DocumentState::Changed);

  world.shared.documentBuilder->update({}, {});
  CHECK(counter->calls.load() == 1);
  CHECK(installed->state.load() == cursiv::DocumentState::Validated);
  CHECK(installed->textDocument().getText() == "datatype String\n");
}

// The folder of `FakeFiles`, whose second file is found and then refuses to
// be read: what a file deleted or locked between the event and the build is.
class HalfReadableFiles final : public cursiv::FileSystemProvider {
 public:
  [[nodiscard]] cursiv::FileSystemNode stat(
      std::string_view uri) const override {
    return _found.stat(uri);
  }
  [[nodiscard]] bool exists(std::string_view uri) const override {
    return _found.exists(uri);
  }
  [[nodiscard]] std::vector<cursiv::FileSystemNode> readDirectory(
      std::string_view uri) const override {
    return _found.readDirectory(uri);
  }
  [[nodiscard]] std::string readFile(std::string_view uri) const override {
    if (uri == FakeFiles::kB) throw cursiv::FileSystemError("gone");
    return _found.readFile(uri);
  }
  [[nodiscard]] std::vector<std::uint8_t> readBinary(
      std::string_view uri) const override {
    return _found.readBinary(uri);
  }

 private:
  FakeFiles _found;
};

// A new file that can no longer be read when the update reaches it is left
// out of the store and said, and the others are built; a subscriber is not
// told of a change the store does not hold.
void a_new_file_that_cannot_be_read_is_said_and_left_out() {
  struct Capture final : cursiv::ObservabilitySink {
    std::vector<cursiv::Observation> seen;
    void publish(const cursiv::Observation& observation) noexcept override {
      try {
        seen.push_back(observation);
      } catch (...) {
      }
    }
  };
  World world;
  world.shared.fileSystemProvider = std::make_unique<HalfReadableFiles>();
  auto sink = std::make_unique<Capture>();
  const Capture* const capture = sink.get();
  world.shared.observabilitySink = std::move(sink);
  struct Told final : cursiv::DocumentUpdateListener {
    std::vector<std::string> changed;
    void onUpdate(std::span<const std::string_view> c,
                  std::span<const std::string_view>) override {
      changed.assign(c.begin(), c.end());
    }
  } told;
  world.shared.documentBuilder->addDocumentUpdateListener(told);
  const std::string_view changed[] = {FakeFiles::kFolder};
  world.shared.documentBuilder->update(changed, {});
  world.shared.documentBuilder->removeDocumentUpdateListener(told);

  const auto a = world.shared.documents->getDocument(FakeFiles::kA);
  CHECK(a != nullptr && a->state.load() == cursiv::DocumentState::Validated);
  CHECK(!world.shared.documents->hasDocument(FakeFiles::kB));
  CHECK(world.shared.documents->all().size() == 1);
  CHECK(capture->seen.size() == 1);
  CHECK(!capture->seen.empty() &&
        capture->seen[0].code ==
            cursiv::ObservationCode::WorkspaceDocumentReadFailed &&
        capture->seen[0].uri == FakeFiles::kB);
  CHECK(told.changed == std::vector<std::string>{std::string(FakeFiles::kA)});
}

// A wait on one document is answered at that document's rung, not when the
// phase drains: the open document, carried first, serves its requests while
// the rest of the workspace is still in the phase.
void a_document_wait_is_answered_at_its_own_rung() {
  using namespace std::chrono_literals;
  World world;
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(0);
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const b = world.add("file:///b.ws", "datatype B\n");
  a->isOpen = true;
  auto waiter = std::async(std::launch::async, [&] {
    return world.shared.documentBuilder->waitUntil(
        cursiv::DocumentState::IndexedContent, a->id);
  });
  CHECK(waiter.wait_for(20ms) == std::future_status::timeout);

  // Holds `b` in the phase until the wait on `a` is answered, five seconds
  // at most: a wait that sleeps to the phase's end is not answered here.
  struct Hold final : cursiv::DocumentPhaseListener {
    const cursiv::Document* held = nullptr;
    std::future<cursiv::WaitOutcome>* waiter = nullptr;
    bool answered = false;
    void onDocumentPhase(cursiv::Document& document,
                         cursiv::DocumentState reached,
                         const cursiv::CancellationToken&) override {
      if (&document != held || reached != cursiv::DocumentState::IndexedContent)
        return;
      answered = waiter->wait_for(5s) == std::future_status::ready;
    }
  } hold;
  hold.held = b;
  hold.waiter = &waiter;
  world.shared.documentBuilder->addDocumentPhaseListener(hold);
  cursiv::Document* const set[] = {b, a};
  world.shared.documentBuilder->build(set);
  world.shared.documentBuilder->removeDocumentPhaseListener(hold);
  CHECK(hold.answered);
  CHECK(waiter.get() == cursiv::WaitOutcome::Reached);
}

#if !defined(NDEBUG) && !defined(__wasi__)
// Loads a file into the store from the linking phase: what the store's
// contract forbids.
struct LoadingScope final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;
  const cursiv::AstNodeDescription* getScopeEntry(
      const cursiv::ReferenceInfo& context,
      std::string_view name) const override {
    (void)services.shared.documents->getOrCreateDocument(
        "file:///lib/" + std::string(name) + ".ws");
    return DefaultScopeProvider::getScopeEntry(context, name);
  }
};

// The store is mutated from a write, before its settle; a build phase that
// does it races every worker and reader beside it, and a debug build stops
// it at the door. Run in a child, the abort being the verdict.
void the_store_refuses_a_load_from_a_build_phase() {
  const pid_t child = fork();
  if (child == 0) {
    // The abort is expected: no core for it.
    const rlimit none{0, 0};
    (void)setrlimit(RLIMIT_CORE, &none);
    (void)std::freopen("/dev/null", "w", stderr);
    const World world([](cursiv::CoreServices& language) {
      language.scopeProvider = std::make_unique<LoadingScope>(language);
    });
    cursiv::Document* const set[] = {
        world.add("file:///u.ws", "entity E { f : L }\n")};
    world.shared.workspaceLock->write([&](const cursiv::CancellationToken& t) {
      world.shared.documentBuilder->build(set, {}, t);
    });
    std::_Exit(0);
  }
  int status = 0;
  (void)waitpid(child, &status, 0);
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
#endif

}  // namespace

void run() {
  a_default_workspace_build_uses_its_workers();
#if !defined(__wasi__) || defined(_REENTRANT)
  exported_symbols_are_collected_in_parallel();
  phase_callbacks_read_after_homonym_invalidation();
#endif
  exports_keep_uri_order(false);
  exports_keep_uri_order(true);
  an_update_honors_an_overridden_affected_check();
  a_new_homonym_retargets_without_reparsing(false, false);
  a_new_homonym_retargets_without_reparsing(true, false);
  a_new_homonym_retargets_without_reparsing(false, true);
  a_new_homonym_retargets_without_reparsing(true, true);
  cancelling_after_an_export_does_not_leave_stale_links(false);
  cancelling_after_an_export_does_not_leave_stale_links(true);
  a_reset_to_computed_scopes_keeps_lazy_resolution();
  a_build_from_linked_reindexes_its_resolutions(false);
  a_build_from_linked_reindexes_its_resolutions(true);
  an_update_drops_unindexed_resolutions(cursiv::DocumentState::ComputedScopes,
                                        false);
  an_update_drops_unindexed_resolutions(cursiv::DocumentState::ComputedScopes,
                                        true);
  an_update_drops_unindexed_resolutions(cursiv::DocumentState::Linked, false);
  an_update_drops_unindexed_resolutions(cursiv::DocumentState::Linked, true);
  deferred_callbacks_are_drained_before_failure();
  concurrent_linking_waits_for_a_candidates_own_reference();
  references_to_a_later_document_can_resolve_lazily();
  a_reference_read_while_scoping_is_read_again();
  factory_then_build_parses_once();
  a_new_file_and_a_recreated_file_are_parsed_once();
  a_build_climbs_every_rung();
  the_rungs_are_announced_in_order();
  what_does_not_resolve_is_said();
  a_reset_forgets_what_was_derived();
  an_update_rebuilds_what_aimed_at_the_change();
  an_update_clears_the_incoming_edge_before_deleting_its_target();
  a_relinked_document_takes_the_updates_request();
  a_reset_forgets_an_interrupted_request();
  an_update_finishes_an_interrupted_request();
  a_new_file_waits_unparsed_for_the_parse_phase();
  a_new_file_that_cannot_be_read_is_said_and_left_out();
  a_document_wait_is_answered_at_its_own_rung();
#if !defined(NDEBUG) && !defined(__wasi__)
  the_store_refuses_a_load_from_a_build_phase();
#endif
  every_document_is_announced_on_its_own();
  the_scopes_are_barred_before_the_first_link();
  a_changed_folder_takes_its_documents_down();
  an_interrupted_request_is_not_replaced();
  a_duplicate_in_the_set_builds_once();
  a_wait_answers_all_its_outcomes();
  a_wait_can_precede_the_first_build();
  a_document_knows_its_links_are_broken();
  a_failed_build_answers_its_waiters();
  a_failed_update_listener_answers_the_waiters(false);
  a_failed_update_listener_answers_the_waiters(true);
  a_request_reads_beside_a_running_build();
  a_request_reads_the_index_beside_a_running_build();
  a_build_cut_after_linking_relinks_next_time();
  a_cut_update_leaves_no_half_reset();
  a_cut_update_takes_the_workspace_rung_down();
  a_rung_crossed_once_is_announced_once();
  a_deleted_documents_request_dies_with_it();
  a_reset_to_parsed_takes_the_exports_down();
  every_listener_hears_a_rung();
  a_build_announces_what_it_builds();
  a_build_without_validation_is_complete();
  an_update_builds_with_the_update_options();
}

}  // namespace document_builder

// Pins where a URI becomes a parsed document: the URI normalized, the language
// routed through the registry, the bytes read once, the tree cut from them.
namespace document_factory {

namespace {

const std::string kSource = "datatype String\nentity A { x : String }\n";

// A workspace with one language and a disk under it.
struct World {
  cursiv::SharedCoreServices shared;
  cursiv::DefaultDocumentFactory factory{shared};

  explicit World(bool withDisk) {
    // The registration gate refuses an incomplete shared container, so the
    // defaults come first; the hand-made pieces below replace them.
    cursiv::install_default_shared_core_services(shared);
    shared.documents = std::make_unique<cursiv::DefaultDocuments>(shared);
    auto registry = std::make_unique<cursiv::DefaultServiceRegistry>(shared);
    auto language = std::make_unique<cursiv::CoreServices>(shared);
    language->languageMetaData = {"workspace", {".ws"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    language->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    registry->registerServices(std::move(language));
    shared.serviceRegistry = (std::move(registry));
    if (withDisk)
      shared.fileSystemProvider =
          std::make_unique<cursiv::LocalFileSystemProvider>();
    else
      shared.fileSystemProvider =
          std::make_unique<cursiv::EmptyFileSystemProvider>();
  }
};

template <class F>
bool throws(F&& f) {
  try {
    f();
  } catch (...) {
    return true;
  }
  return false;
}

void a_text_becomes_a_parsed_document() {
  World world(false);
  const auto d = world.factory.fromString(kSource, "file:///w/a.ws");

  CHECK(d != nullptr);
  CHECK(d->uri == "file:///w/a.ws");
  CHECK(d->state.load() == cursiv::DocumentState::Parsed);
  CHECK(d->parseSucceeded());
  CHECK(d->hasAst());
  CHECK(!d->parseRecovered());
  // The language routed by extension stamped the text document.
  CHECK(d->textDocument().languageId() == "workspace");
  CHECK(d->textDocument().version() == 0);
  // Not stored: the factory creates, `Documents` stores.
  CHECK(!world.shared.documents->hasDocument("file:///w/a.ws"));

  // The root's parent is the document: climbing a tree lands on it, and that
  // is how any node finds the grammar that gives its element ids meaning.
  CHECK(d->parseResult.value->parent == d.get());
  CHECK(d->parseResult.value->getDocument() == d.get());
  // From a node deeper in, too.
  const cursiv::AstNode* deep = d->parseResult.nodes.back();
  CHECK(deep->getDocument() == d.get());
  // A node built by hand belongs to no document.
  const cursiv::AstNode loose;
  CHECK(loose.getDocument() == nullptr);
}

// A spelling the caller wrote is not the name the document is known by.
void the_uri_is_normalized_once() {
  World world(false);
  const auto d = world.factory.fromString(kSource, "file:///w/./x/../a.ws");
  CHECK(d->uri == "file:///w/a.ws");
  CHECK(d->textDocument().uri() == "file:///w/a.ws");
}

void a_language_nobody_registered_has_no_document() {
  World world(false);
  CHECK(
      throws([&] { (void)world.factory.fromString("x", "file:///w/a.txt"); }));
}

void a_broken_text_is_still_a_document() {
  World world(false);
  const auto d = world.factory.fromString(
      "datatype String\nentity A { x  String }\n", "file:///w/a.ws");
  CHECK(d->hasAst());
  CHECK(d->parseRecovered());
  CHECK(!d->parseResult.diagnostics.empty());
}

void a_uri_is_read_from_the_disk_it_names() {
  // The test's own build directory, not bare /tmp: a fixed name there
  // collides between two runs sharing one machine.
  const std::filesystem::path dir =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "cursiv_factory_lane";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "a.ws");
    out << kSource;
  }
  const std::string uri = cursiv::path_to_file_uri((dir / "a.ws").string());

  World world(true);
  const auto d = world.factory.fromUri(uri);
  CHECK(d->uri == uri);
  CHECK(d->textDocument().getText() == kSource);
  CHECK(d->parseSucceeded());

  // Nothing readable behind it is not a document.
  CHECK(throws([&] {
    (void)world.factory.fromUri(
        cursiv::path_to_file_uri((dir / "gone.ws").string()));
  }));
  std::filesystem::remove_all(dir);
}

// A workspace with no disk cannot answer `fromUri`, and says so.
void without_a_disk_a_uri_is_not_readable() {
  World world(false);
  CHECK(throws([&] { (void)world.factory.fromUri("file:///w/a.ws"); }));
}

// A closed document's truth is the disk: an update reads it again, and a
// document nothing readable stands behind keeps the text it holds.
void an_update_reads_the_disk_again() {
  const std::filesystem::path dir =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "cursiv_factory_update";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "a.ws");
    out << kSource;
  }
  const std::string uri = cursiv::path_to_file_uri((dir / "a.ws").string());
  World world(true);
  const auto d = world.factory.fromUri(uri);
  const std::string edited = "datatype Other\n";
  {
    std::ofstream out(dir / "a.ws");
    out << edited;
  }
  const auto revision = d->buildRevision;
  world.factory.update(*d);
  CHECK(d->buildRevision == revision + 1);
  CHECK(d->textDocument().getText() == edited);
  CHECK(d->parseSucceeded());

  // An open tab outranks the disk.
  d->isOpen = true;
  {
    std::ofstream out(dir / "a.ws");
    out << kSource;
  }
  world.factory.update(*d);
  CHECK(d->textDocument().getText() == edited);

  // Gone from the disk, the text held is what there is.
  d->isOpen = false;
  std::filesystem::remove_all(dir);
  world.factory.update(*d);
  CHECK(d->textDocument().getText() == edited);
}

// The rebuild tears the previous build down: nothing of it survives into the
// next, and the revision moves so a stale key can be told apart.
void an_update_reparses_from_scratch() {
  World world(false);
  const auto d = world.factory.fromString(kSource, "file:///w/a.ws");
  d->diagnostics.push_back({});
  d->state = cursiv::DocumentState::Validated;
  const std::uint64_t before = d->buildRevision;

  world.factory.update(*d);
  CHECK(d->state.load() == cursiv::DocumentState::Parsed);
  // The rebuild re-parents the new root: the old one is gone with its tree.
  CHECK(d->parseResult.value->getDocument() == d.get());
  CHECK(d->diagnostics.empty());
  CHECK(d->buildRevision == before + 1);
  CHECK(d->parseSucceeded());
  CHECK(d->hasAst());
}

void a_cancelled_token_makes_no_document() {
  World world(false);
  cursiv::CancellationTokenSource source;
  source.request_stop();
  // Typed: the abort is a cancellation, not some other failure in disguise.
  bool cancelled = false;
  try {
    (void)world.factory.fromString(kSource, "file:///w/a.ws",
                                   source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
}

// A text the client already versioned comes in whole, version kept.
void a_text_document_becomes_a_parsed_document() {
  World world(false);
  const auto d = world.factory.fromTextDocument(cursiv::TextDocument::create(
      "file:///w/a.ws", "workspace", 7, std::string(kSource)));
  CHECK(d != nullptr);
  CHECK(d->textDocument().version() == 7);
  CHECK(d->parseSucceeded() && d->hasAst());
}

// Unlike `fromString`, `fromTextDocument` does not renormalize the
// `TextDocument` it is handed: `d->uri` is canonical, its own text document's
// is not.
void from_text_document_keeps_the_callers_uri_spelling() {
  World world(false);
  const auto d = world.factory.fromTextDocument(cursiv::TextDocument::create(
      "file:///w/./x/../a.ws", "workspace", 0, kSource));
  CHECK(d->uri == "file:///w/a.ws");
  // Pinned as is: see G3-6
  CHECK(d->textDocument().uri() == "file:///w/./x/../a.ws");
}

// The storey holding the client's newest text installs it here: same object,
// same id, back to `Changed` with nothing parsed yet.
void an_attached_text_takes_the_document_back_down() {
  World world(false);
  const auto d = world.factory.fromString(kSource, "file:///w/a.ws");
  d->state = cursiv::DocumentState::Validated;
  const cursiv::DocumentId id = d->id;

  world.factory.attachTextDocument(
      *d, cursiv::TextDocument::create("file:///w/a.ws", "workspace", 2,
                                       "datatype Fresh\n"));
  CHECK(d->id == id);
  CHECK(d->state.load() == cursiv::DocumentState::Changed);
  CHECK(d->textDocument().getText() == "datatype Fresh\n");
  CHECK(d->textDocument().version() == 2);
}

}  // namespace

void run() {
  a_text_becomes_a_parsed_document();
  the_uri_is_normalized_once();
  a_language_nobody_registered_has_no_document();
  a_broken_text_is_still_a_document();
  a_uri_is_read_from_the_disk_it_names();
  without_a_disk_a_uri_is_not_readable();
  an_update_reparses_from_scratch();
  an_update_reads_the_disk_again();
  a_cancelled_token_makes_no_document();
  a_text_document_becomes_a_parsed_document();
  from_text_document_keeps_the_callers_uri_spelling();
  an_attached_text_takes_the_document_back_down();
}

}  // namespace document_factory

// Pins what a document answers before anything has been derived from its text:
// the URI it is known by, and the ladder's bottom rung.
namespace document {

namespace {

cursiv::TextDocument text(std::string uri) {
  return cursiv::TextDocument::create(std::move(uri), "t", 1, "abc");
}

// The text document names the document, unless the caller names it otherwise.
void the_uri_comes_from_the_text() {
  const cursiv::Document a(text("file:///a"));
  CHECK(a.uri == "file:///a");
  CHECK(a.textDocument().getText() == "abc");

  const cursiv::Document b(text("file:///b"), "file:///override");
  CHECK(b.uri == "file:///override");
}

void a_fresh_document_has_derived_nothing() {
  const cursiv::Document d(text("file:///a"));
  CHECK(d.id == cursiv::kNoDocument);
  CHECK(d.state.load() == cursiv::DocumentState::Changed);
  CHECK(!d.hasAst());
  CHECK(!d.parseSucceeded());
  CHECK(!d.parseRecovered());
  CHECK(d.diagnostics.empty());
  CHECK(d.buildRevision == 0);
  // The result exists -- it is a value, not a pointer -- and is empty.
  CHECK(d.parseResult.cst().size() == 0);
  CHECK(!static_cast<bool>(d.parseResult));
}

// Every phase writes the field it computes; nothing hands out a setter.
// A conversion that failed is not a document the parser repaired.
void only_syntax_counts_as_recovered() {
  cursiv::Document d(text("file:///a"));
  cursiv::ParseDiagnostic bad;
  bad.kind = cursiv::DiagnosticKind::conversion_error;
  d.parseResult.diagnostics.push_back(bad);
  CHECK(!d.parseRecovered());

  cursiv::ParseDiagnostic bound;
  bound.kind = cursiv::DiagnosticKind::bounded;
  d.parseResult.diagnostics.push_back(bound);
  CHECK(!d.parseRecovered());

  cursiv::ParseDiagnostic repair;
  repair.kind = cursiv::DiagnosticKind::deleted;
  d.parseResult.diagnostics.push_back(repair);
  CHECK(d.parseRecovered());
}

void a_phase_writes_its_own_field() {
  cursiv::Document d(text("file:///a"));
  d.state = cursiv::DocumentState::Validated;
  cursiv::Diagnostic bad;
  bad.message = "boom";
  bad.range = cursiv::TextRange{0, 3};
  d.diagnostics.push_back(std::move(bad));
  CHECK(d.state.load() == cursiv::DocumentState::Validated);
  CHECK(d.diagnostics.size() == 1);
  CHECK(d.diagnostics[0].message == "boom");
}

}  // namespace

void run() {
  the_uri_comes_from_the_text();
  a_fresh_document_has_derived_nothing();
  only_syntax_counts_as_recovered();
  a_phase_writes_its_own_field();
}

}  // namespace document

// Pins the store of a workspace's managed documents. Two spellings of one file
// are one id and one document, and an id outlives the document it named.
namespace documents {

namespace {

// A complete container behind a store of its own: the lanes below exercise
// the store alone, and every service it reaches is installed, as the registry
// requires of any container a service is reached through.
cursiv::SharedCoreServices& completeShared() {
  static cursiv::SharedCoreServices shared;
  static const bool installed = [] {
    cursiv::install_default_shared_core_services(shared);
    return true;
  }();
  (void)installed;
  return shared;
}

std::shared_ptr<cursiv::Document> document(std::string uri) {
  return std::make_shared<cursiv::Document>(
      cursiv::TextDocument::create(std::move(uri), "t", 1, "abc"));
}

// The held document comes back without the factory being reached for, which is
// what makes the door cheap on the path that matters -- a reference naming a
// file already open.
void get_or_create_returns_the_held_document() {
  cursiv::DefaultDocuments store{completeShared()};
  const auto d = document("file:///a/b.t");
  store.addDocument(d);

  CHECK(store.getOrCreateDocument("file:///a/b.t") == d);
}

// Nothing held is nothing to invalidate, and no builder is needed to say so.
void invalidating_an_unknown_uri_yields_nothing() {
  cursiv::DefaultDocuments store{completeShared()};
  CHECK(store.invalidateDocument("file:///nowhere.t") == nullptr);
}

void a_document_is_found_by_uri_and_by_id() {
  cursiv::DefaultDocuments store{completeShared()};
  const auto d = document("file:///a/b.t");
  store.addDocument(d);

  CHECK(store.hasDocument("file:///a/b.t"));
  CHECK(store.getDocument("file:///a/b.t") == d);
  CHECK(store.getDocument(d->id) == d);
  CHECK(store.getDocumentUri(d->id) == "file:///a/b.t");
  CHECK(store.all().size() == 1);

  CHECK(!store.hasDocument("file:///a/other.t"));
  CHECK(store.getDocument("file:///a/other.t") == nullptr);
  CHECK(store.getDocumentId("file:///a/other.t") == cursiv::kNoDocument);
  CHECK(store.getDocument(cursiv::kNoDocument) == nullptr);
}

// The store normalizes at every gate, or one file would be two documents.
void one_file_is_one_id_however_it_is_spelled() {
  cursiv::DefaultDocuments store{completeShared()};
  const auto d = document("file:///a/b.t");
  store.addDocument(d);

  CHECK(store.getDocument("file:///a/./x/../b.t") == d);
  CHECK(store.getDocumentId("file:///a/x/../b.t") == d->id);
  CHECK(store.getOrCreateDocumentId("file:///a/b.t") == d->id);
  CHECK(store.all().size() == 1);
}

// A scheme has no case, whatever it names: the store's gates and
// `contains_uri` agree that both spellings are one document.
void one_scheme_is_one_id_whatever_its_case() {
  cursiv::DefaultDocuments store{completeShared()};
  const auto d = document("MEM:///w/a.t");
  store.addDocument(d);

  CHECK(cursiv::contains_uri("mem:///w", d->uri));
  CHECK(store.getDocument("mem:///w/a.t") == d);
  CHECK(store.getDocuments("mem:///w").size() == 1);
  CHECK(store.getDocuments("MEM:///w").size() == 1);
  CHECK(store.getOrCreateDocumentId("mem:///w/a.t") == d->id);
  bool refused = false;
  try {
    store.addDocument(document("mem:///w/a.t"));
  } catch (const cursiv::DocumentStoreError&) {
    refused = true;
  }
  CHECK(refused);
  CHECK(store.all().size() == 1);
}

// An id can exist before a document does, and still after it is gone: a
// description that outlives its document must stay nameable.
void an_id_outlives_its_document() {
  cursiv::DefaultDocuments store{completeShared()};
  const cursiv::DocumentId named = store.getOrCreateDocumentId("file:///a/b.t");
  CHECK(named != cursiv::kNoDocument);
  CHECK(store.getDocument(named) == nullptr);
  CHECK(!store.hasDocument("file:///a/b.t"));
  CHECK(store.getDocumentUri(named) == "file:///a/b.t");

  // The document lands on the id its URI already had.
  const auto d = document("file:///a/b.t");
  store.addDocument(d);
  CHECK(d->id == named);

  const auto removed = store.deleteDocument("file:///a/b.t");
  CHECK(removed == d);
  CHECK(!store.hasDocument("file:///a/b.t"));
  CHECK(store.all().empty());
  // The id still names its URI, and a re-add lands on it again.
  CHECK(store.getDocumentUri(named) == "file:///a/b.t");
  CHECK(store.getDocumentId("file:///a/b.t") == named);
  store.addDocument(document("file:///a/b.t"));
  CHECK(store.getDocument(named)->id == named);
}

// The caller holds what it removed: a reader mid-flight is not left dangling.
void a_removed_document_stays_alive_for_its_holder() {
  cursiv::DefaultDocuments store{completeShared()};
  store.addDocument(document("file:///a/b.t"));
  const auto held = store.deleteDocument("file:///a/b.t");
  CHECK(held != nullptr);
  CHECK(held->textDocument().getText() == "abc");
  CHECK(store.deleteDocument("file:///a/b.t") == nullptr);
  CHECK(store.deleteDocument(cursiv::kNoDocument) == nullptr);
}

void ids_are_never_reused() {
  cursiv::DefaultDocuments store{completeShared()};
  const cursiv::DocumentId first = store.getOrCreateDocumentId("file:///a/1.t");
  const cursiv::DocumentId second =
      store.getOrCreateDocumentId("file:///a/2.t");
  CHECK(first != second);
  (void)store.deleteDocument(first);
  CHECK(store.getOrCreateDocumentId("file:///a/3.t") != first);
  CHECK(store.getDocumentUri(first) == "file:///a/1.t");
}

// Refused, for the reason `deleteDocument` states: a silent replacement
// destroys the document while the index rows describing it stand.
void one_uri_is_added_once() {
  cursiv::DefaultDocuments store{completeShared()};
  store.addDocument(document("file:///a/b.t"));
  bool refused = false;
  try {
    store.addDocument(document("file:///a/b.t"));
  } catch (const cursiv::DocumentStoreError&) {
    refused = true;
  }
  CHECK(refused);
  // ...and the one already held is still the one held.
  CHECK(store.getDocument("file:///a/b.t") != nullptr);
}

// The branch that reaches for the factory: a URI nobody holds is read, built
// into a document, and held from then on -- one read, not two.
void get_or_create_reads_the_disk_when_nothing_is_held() {
  cursiv::SharedCoreServices world;
  auto memory = std::make_unique<cursiv::MemoryFileSystemProvider>();
  memory->addFile("file:///w/a.ws", "datatype A\n");
  world.fileSystemProvider = std::move(memory);
  cursiv::install_default_shared_core_services(world);
  auto language = std::make_unique<cursiv::CoreServices>(world);
  language->languageMetaData = {"workspace", {".ws"}, {}};
  language->parser = ws::generated::Workspace::create_parser();
  language->astReflection = ws::generated::Workspace::create_ast_reflection();
  cursiv::install_default_core_services(*language);
  world.serviceRegistry->registerServices(std::move(language));

  const auto created = world.documents->getOrCreateDocument("file:///w/a.ws");
  CHECK(created != nullptr);
  CHECK(created->textDocument().getText() == "datatype A\n");
  CHECK(world.documents->getOrCreateDocument("file:///w/a.ws") == created);

  // ...and `createDocument` builds from the text it is handed, no disk at all.
  const auto typed =
      world.documents->createDocument("file:///w/b.ws", "datatype B\n");
  CHECK(typed != nullptr);
  CHECK(typed->textDocument().getText() == "datatype B\n");
  CHECK(world.documents->getDocument("file:///w/b.ws") == typed);
}

// A folder is a prefix: its documents come back together, and deleting it
// takes them and nobody else's.
void a_folder_answers_and_deletes_as_one() {
  cursiv::DefaultDocuments store{completeShared()};
  store.addDocument(document("file:///w/a/x.t"));
  store.addDocument(document("file:///w/a/y.t"));
  store.addDocument(document("file:///w/b/z.t"));

  CHECK(store.getDocuments("file:///w/a").size() == 2);
  CHECK(store.getDocuments("file:///w").size() == 3);

  const auto gone = store.deleteDocuments("file:///w/a");
  CHECK(gone.size() == 2);
  CHECK(store.getDocuments("file:///w/a").empty());
  CHECK(store.getDocument("file:///w/b/z.t") != nullptr);
}

}  // namespace

void run() {
  a_document_is_found_by_uri_and_by_id();
  get_or_create_returns_the_held_document();
  invalidating_an_unknown_uri_yields_nothing();
  one_file_is_one_id_however_it_is_spelled();
  one_scheme_is_one_id_whatever_its_case();
  an_id_outlives_its_document();
  a_removed_document_stays_alive_for_its_holder();
  ids_are_never_reused();
  one_uri_is_added_once();
  get_or_create_reads_the_disk_when_nothing_is_held();
  a_folder_answers_and_deletes_as_one();
}

}  // namespace documents

// Pins how a workspace reads what it did not open: a `file:` URI names a path
// or the read fails, and nothing is ever guessed from a URI it cannot resolve.
namespace file_system_provider {

namespace {

template <class F>
bool throws(F&& f) {
  try {
    f();
  } catch (const cursiv::FileSystemError&) {
    return true;
  }
  return false;
}

// Every read fails and nothing exists -- what an embedder installs when
// documents only ever arrive as text.
void an_empty_workspace_has_no_disk() {
  const cursiv::EmptyFileSystemProvider fs;
  CHECK(!fs.exists("file:///a/b.txt"));
  CHECK(throws([&] { (void)fs.readFile("file:///a/b.txt"); }));
  CHECK(throws([&] { (void)fs.readBinary("file:///a/b.txt"); }));
  CHECK(throws([&] { (void)fs.stat("file:///a/b.txt"); }));
  // Not an error: an empty workspace has directories with nothing in them.
  CHECK(fs.readDirectory("file:///a").empty());
}

void a_uri_that_names_no_path_is_a_failed_read() {
  const cursiv::LocalFileSystemProvider fs;
  CHECK(throws([&] { (void)fs.readFile("http://example.com/a.txt"); }));
  CHECK(throws([&] { (void)fs.exists("http://example.com/a.txt"); }));
  CHECK(throws([&] { (void)fs.stat("untitled:one"); }));
}

void what_is_on_disk_reads_back() {
  // The test's own build directory, not bare /tmp: a fixed name there
  // collides between two runs sharing one machine.
  const std::filesystem::path dir =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "cursiv_fs_lane";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "sub");
  {
    std::ofstream out(dir / "a.txt");
    out << "hello";
  }

  const cursiv::LocalFileSystemProvider fs;
  const std::string fileUri =
      cursiv::path_to_file_uri((dir / "a.txt").string());
  const std::string dirUri = cursiv::path_to_file_uri(dir.string());

  CHECK(fs.exists(fileUri));
  CHECK(fs.readFile(fileUri) == "hello");
  CHECK(fs.readBinary(fileUri).size() == 5);

  const cursiv::FileSystemNode node = fs.stat(fileUri);
  CHECK(node.isFile);
  CHECK(!node.isDirectory);
  // The URI it reports is the canonical one, whatever spelling came in.
  CHECK(node.uri == fileUri);
  CHECK(fs.stat(dirUri).isDirectory);

  // Absent is false, not an error.
  CHECK(!fs.exists(cursiv::path_to_file_uri((dir / "nope.txt").string())));
  CHECK(throws([&] {
    (void)fs.readFile(cursiv::path_to_file_uri((dir / "nope.txt").string()));
  }));
  // A folder is not readable as a file, and says so as this interface does.
  bool typed = false;
  try {
    (void)fs.readFile(dirUri);
  } catch (const cursiv::FileSystemError&) {
    typed = true;
  }
  CHECK(typed);

  const std::vector<cursiv::FileSystemNode> entries = fs.readDirectory(dirUri);
  CHECK(entries.size() == 2);
  bool sawFile = false, sawDir = false;
  for (const cursiv::FileSystemNode& e : entries) {
    sawFile = sawFile || (e.isFile && e.uri == fileUri);
    sawDir = sawDir || e.isDirectory;
  }
  CHECK(sawFile);
  CHECK(sawDir);

  // A link to the parent is an entry, not a folder to traverse again.
  std::error_code symlinkError;
  std::filesystem::create_directory_symlink(dir, dir / "sub" / "loop", symlinkError);
  if (!symlinkError) {
    const auto linked = fs.readDirectory(
        cursiv::path_to_file_uri((dir / "sub").string()));
    CHECK(linked.size() == 1);
    CHECK(!linked.front().isDirectory && !linked.front().isFile);
  }

  std::filesystem::remove_all(dir);
}

// An escaped NUL names no file: the system call would stop at it and read
// the file whose name ends there, under the URI of another.
void an_escaped_nul_names_no_file() {
  const std::filesystem::path dir =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "cursiv_fs_nul";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  { std::ofstream(dir / "secret") << "not a .ws file"; }
  const std::string uri =
      cursiv::path_to_file_uri((dir / "secret").string()) + "%00.ws";
  CHECK(!cursiv::file_uri_to_path(uri).has_value());
  const cursiv::LocalFileSystemProvider fs;
  CHECK(throws([&] { (void)fs.readFile(uri); }));
  std::filesystem::remove_all(dir);
}

#if defined(__linux__)
void size_is_a_hint_and_read_errors_are_reported() {
  const cursiv::LocalFileSystemProvider fs;
  // procfs exposes regular files whose advertised size is zero. No timing
  // dependency is needed to exercise a read beyond the initial allocation.
  if (std::filesystem::exists("/proc/self/status")) {
    CHECK(std::filesystem::file_size("/proc/self/status") == 0);
    CHECK(fs.readFile("file:///proc/self/status").starts_with("Name:"));
    CHECK(!fs.readBinary("file:///proc/self/status").empty());
  }
  // Reading the unmapped zero page fails with EIO, after open succeeds.
  if (std::filesystem::exists("/proc/self/mem"))
    CHECK(throws([&] { (void)fs.readBinary("file:///proc/self/mem"); }));
}
#endif

}  // namespace

void run() {
  an_empty_workspace_has_no_disk();
  a_uri_that_names_no_path_is_a_failed_read();
  what_is_on_disk_reads_back();
  an_escaped_nul_names_no_file();
#if defined(__linux__)
  size_is_a_hint_and_read_errors_are_reported();
#endif
}

}  // namespace file_system_provider

// Pins what the whole workspace knows: the symbols each document exports, and
// the references each one holds -- the inverse of the linker's edge.
namespace index_manager {

namespace {

struct World {
  cursiv::SharedCoreServices shared;
  cursiv::CoreServices* ws = nullptr;

  World() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_core_services(shared);
    auto language = std::make_unique<cursiv::CoreServices>(shared);
    language->languageMetaData = {"workspace", {".ws"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    ws = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }

  // A document taken all the way to `IndexedReferences`.
  std::shared_ptr<cursiv::Document> built(const std::string& uri,
                                          std::string text) const {
    auto document =
        shared.documentFactory->fromString(std::move(text), std::move(uri));
    shared.documents->addDocument(document);
    shared.indexManager->updateContent(*document);
    document->localSymbols =
        ws->scopeComputation->collectLocalSymbols(*document);
    ws->linker->link(*document);
    shared.indexManager->updateReferences(*document);
    document->state = cursiv::DocumentState::IndexedReferences;
    return document;
  }

  std::vector<std::string> exported(
      const cursiv::AstTypeTag* type = nullptr) const {
    std::vector<std::string> names;
    (void)shared.indexManager->visitElements(
        type, {}, [&names](const cursiv::AstNodeDescription& d) {
          names.emplace_back(d.name);
          return true;
        });
    return names;
  }
};

bool has(const std::vector<std::string>& all, std::string_view name) {
  for (const std::string& one : all)
    if (one == name) return true;
  return false;
}

void a_document_exports_its_top_level() {
  const World world;
  const auto document = world.built(
      "file:///a.ws", "datatype String\nentity Person { age : String }\n");

  const std::vector<std::string> names = world.exported();
  CHECK(has(names, "String"));
  CHECK(has(names, "Person"));
  CHECK(!has(names, "age"));  // nested: not the world's business
  (void)document;
}

void a_type_filter_includes_derived_types() {
  const World world;
  const auto document = world.built(
      "file:///a.ws", "datatype String\nentity Person { age : String }\n");
  const auto types = world.exported(&cursiv::ast_type_tag<ws::ast::Type>);
  CHECK(types.size() == 2);
  CHECK(has(types, "String") && has(types, "Person"));
  const auto datatypes = world.exported(&cursiv::ast_type_tag<ws::ast::DataType>);
  CHECK(datatypes.size() == 1 && has(datatypes, "String"));
  CHECK(world.exported(&cursiv::ast_type_tag<ws::ast::Feature>).empty());
}

void homonyms_follow_normalized_uri_order() {
  for (unsigned permutation = 0; permutation < 4; ++permutation) {
    const World world;
    const auto first = world.shared.documentFactory->fromString(
        "datatype Same\ndatatype Same\ndatatype FirstOnly\n",
        "file:///z/../a.ws");
    const auto second = world.shared.documentFactory->fromString(
        "datatype Same\ndatatype SecondOnly\n", "file:///b.ws");
    CHECK(first->uri == "file:///a.ws");
    const bool reverseIds = (permutation & 1) != 0;
    world.shared.documents->addDocument(reverseIds ? second : first);
    world.shared.documents->addDocument(reverseIds ? first : second);
    CHECK((first->id < second->id) != reverseIds);

    auto& index = *world.shared.indexManager;
    const auto& reflection = *world.ws->astReflection;
    const bool reversePublication = (permutation & 2) != 0;
    index.updateContent(reversePublication ? *second : *first);
    index.updateContent(reversePublication ? *first : *second);
    const auto firstExports =
        world.ws->scopeComputation->collectExportedSymbols(*first);
    const auto secondExports =
        world.ws->scopeComputation->collectExportedSymbols(*second);
    CHECK(firstExports.size() == 3 && secondExports.size() == 2);
    if (firstExports.size() != 3 || secondExports.size() != 2) continue;

    using Symbol = std::pair<cursiv::DocumentId, cursiv::NodeId>;
    const std::vector<Symbol> expected = {
        {first->id, firstExports[0].symbolId},
        {first->id, firstExports[1].symbolId},
        {second->id, secondExports[0].symbolId}};
    const auto checkOrder = [&] {
      const auto* found = index.findByName("Same", true, {}, reflection);
      CHECK(found != nullptr);
      if (found != nullptr)
        CHECK(Symbol(found->documentId, found->symbolId) == expected.front());

      std::vector<Symbol> matches;
      CHECK(index.visitByName(
          "Same", true, {}, reflection,
          [&](const cursiv::AstNodeDescription& description) {
            matches.emplace_back(description.documentId, description.symbolId);
            return true;
          }));
      CHECK(matches == expected);

      const cursiv::DocumentId documents[] = {second->id, first->id};
      std::vector<std::string> names;
      CHECK(index.visitElements(
          nullptr, documents,
          [&](const cursiv::AstNodeDescription& description) {
            names.emplace_back(description.name);
            return true;
          }));
      CHECK((names == std::vector<std::string>{"Same", "Same", "FirstOnly",
                                              "Same", "SecondOnly"}));
    };
    checkOrder();
    index.updateContent(*second);
    index.updateContent(*first);
    checkOrder();
    for (int i = 0; i < 40; ++i)
      (void)world.built("file:///extra" + std::to_string(i) + ".ws",
                        "datatype Extra");
    checkOrder();
    CHECK(index.removeContent(first->id));
    const auto* remaining = index.findByName("Same", true, {}, reflection);
    CHECK(remaining != nullptr && remaining->documentId == second->id);
    index.updateContent(*first);
    checkOrder();
  }
}

// A fresh view fills its shards on demand, from whichever reader needs one
// first: readers racing on the same snapshot must each see all of it.
void racing_readers_see_the_whole_view() {
  const World world;
  std::vector<std::shared_ptr<cursiv::Document>> documents;
  for (int i = 0; i < 64; ++i)
    documents.push_back(
        world.built("file:///d" + std::to_string(i) + ".ws",
                    "datatype Same\ndatatype T" + std::to_string(i) + "\n"));
  const auto& index = *world.shared.indexManager;
  const auto& reflection = *world.ws->astReflection;
  std::atomic<int> wrong{0};
  world.shared.taskScheduler->parallelFor(
      cursiv::CancellationToken{}, std::views::iota(0, 32), [&](int) {
        for (int i = 0; i < 64; ++i) {
          const auto* found =
              index.findByName("T" + std::to_string(i), true, {}, reflection);
          if (found == nullptr || found->documentId != documents[i]->id)
            ++wrong;
        }
        const auto* same = index.findByName("Same", true, {}, reflection);
        if (same == nullptr || same->documentId != documents.front()->id)
          ++wrong;
      });
  CHECK(wrong == 0);
}

// A writer retires the view while readers fill its shards: every answer is
// whole, from the snapshot each reader holds.
void readers_fill_a_view_a_writer_retires() {
  const World world;
  std::vector<std::shared_ptr<cursiv::Document>> documents;
  for (int i = 0; i < 48; ++i) {
    std::string text = "datatype Same\n";
    for (int k = 0; k < 16; ++k)
      text += "datatype N" + std::to_string(i) + "_" + std::to_string(k) + "\n";
    documents.push_back(
        world.built("file:///d" + std::to_string(i) + ".ws", std::move(text)));
  }
  auto& index = *world.shared.indexManager;
  const auto& reflection = *world.ws->astReflection;
  std::atomic<bool> stop{false};
  std::atomic<int> wrong{0};
  std::vector<std::jthread> readers;
  for (unsigned r = 0; r < 4; ++r)
    readers.emplace_back([&, r] {
      unsigned x = 12345u + r;
      while (!stop.load()) {
        x = x * 1103515245u + 12345u;
        const unsigned i = (x >> 8) % 48;
        const std::string name = "n" + std::to_string(i) + "_" +
                                 std::to_string((x >> 16) % 16);
        const auto* found = index.findByName(name, false, {}, reflection);
        if (found == nullptr || found->documentId != documents[i]->id) ++wrong;
        int same = 0;
        (void)index.visitByName("SAME", false, {}, reflection,
                                [&same](const cursiv::AstNodeDescription&) {
                                  ++same;
                                  return true;
                                });
        if (same != 48) ++wrong;
      }
    });
  for (unsigned w = 0; w < 400; ++w) index.updateContent(*documents[w % 48]);
  stop = true;
  readers.clear();
  CHECK(wrong == 0);
}

// The inverse edge: who points at this symbol.
void a_symbol_knows_what_points_at_it() {
  const World world;
  const auto document = world.built(
      "file:///a.ws", "datatype String\nentity Person { age : String }\n");

  // The `String` datatype is what `age : String` names.
  const cursiv::AstNode* const target =
      document->parseResult.references->handles.front().owner;
  (void)target;

  const cursiv::AstNodeDescription* string = nullptr;
  (void)world.shared.indexManager->visitElements(
      nullptr, {}, [&string](const cursiv::AstNodeDescription& d) {
        if (d.name == "String") string = &d;
        return string == nullptr;
      });
  CHECK(string != nullptr);

  std::vector<cursiv::ReferenceDescription> incoming;
  (void)world.shared.indexManager->visitReferences(
      string->documentId, string->symbolId,
      [&](const cursiv::ReferenceDescription& row) {
        incoming.push_back(row);
        return true;
      });
  CHECK(incoming.size() == 1);
  CHECK(incoming.front().local);
  // `Feature::type` names one target, not several.
  CHECK(!incoming.front().multiReference);
  CHECK(incoming.front().sourceDocumentId == document->id);
  CHECK(document->textDocument().getText().substr(
            incoming.front().sourceBegin,
            incoming.front().sourceEnd - incoming.front().sourceBegin) ==
        "String");

  // A symbol nobody names has no incoming edge.
  CHECK(world.shared.indexManager->visitReferences(
      document->id, cursiv::kNoNode,
      [](const cursiv::ReferenceDescription&) { return false; }));
}

// Removing a document takes both halves with it.
void removing_a_document_forgets_it() {
  const World world;
  const auto document = world.built(
      "file:///a.ws", "datatype String\nentity Person { age : String }\n");
  CHECK(!world.exported().empty());

  CHECK(world.shared.indexManager->remove(document->id));
  CHECK(world.exported().empty());
  // Nothing left to remove.
  CHECK(!world.shared.indexManager->remove(document->id));
}

// A change rebuilds only what pointed at it.
void only_what_points_at_a_change_is_affected() {
  const World world;
  const auto a = world.built("file:///a.ws", "datatype String\n");
  const auto b =
      world.built("file:///b.ws", "datatype Other\nentity E { f : Other }\n");

  // `b` names only its own symbols, so `a` changing leaves it alone.
  const std::unordered_set<cursiv::DocumentId> changed = {a->id};
  CHECK(!world.shared.indexManager->isAffected(*b, changed));
  // ...and a local edge is not what affects it: its own change resets it
  // whole, before this is ever asked.
  const std::unordered_set<cursiv::DocumentId> itself = {b->id};
  CHECK(!world.shared.indexManager->isAffected(*b, itself));
}

void affected_checks_resolve_target_mask_collisions() {
  const World world;
  for (unsigned i = 0; i < 63; ++i)
    (void)world.shared.documents->getOrCreateDocumentId(
        "file:///reserved" + std::to_string(i) + ".ws");
  const auto target = world.built("file:///target.ws", "datatype Remote\n");
  const auto dependent = world.built(
      "file:///dependent.ws",
      "datatype Local\nentity Use { remote : Remote local : Local }\n");
  const auto targetId = static_cast<std::uint32_t>(target->id);
  CHECK((targetId & 63) == 63);
  cursiv::DocumentId collision = cursiv::kNoDocument;
  for (unsigned i = 0; i < 64; ++i) {
    const auto id = world.shared.documents->getOrCreateDocumentId(
        "file:///collision" + std::to_string(i) + ".ws");
    if ((static_cast<std::uint32_t>(id) & 63) == (targetId & 63)) {
      collision = id;
      break;
    }
  }
  CHECK(collision != cursiv::kNoDocument && collision != target->id);
  auto& index = *world.shared.indexManager;
  CHECK(!index.isAffected(*dependent, {collision}));
  CHECK(!index.isAffected(*dependent, {dependent->id}));
  CHECK(!index.isAffected(*dependent, {collision, dependent->id}));
  CHECK(index.isAffected(*dependent, {target->id}));
  CHECK(index.isAffected(*dependent, {collision, target->id, dependent->id}));
  CHECK(index.removeReferences(dependent->id));
  CHECK(!index.isAffected(*dependent, {target->id}));
  index.updateReferences(*dependent);
  CHECK(index.isAffected(*dependent, {target->id}));
  CHECK(!index.isAffected(*dependent, {collision}));
}

void imported_names_follow_reference_removal_and_replacement() {
  const World world;
  const auto x = world.built("file:///x.ws", "datatype X\n");
  const auto y = world.built("file:///y.ws", "datatype Y\n");
  const auto a = world.built("file:///a.ws", "entity A { x : X y : Y }\n");
  const auto b = world.built("file:///b.ws", "entity B { x : X y : Y }\n");
  const auto c = world.built("file:///c.ws", "entity C { x : X }\n");
  const auto d = world.built("file:///d.ws", "entity D { y : Y }\n");
  auto& index = *world.shared.indexManager;
  const auto checkAffected = [&](const cursiv::Document& exporter,
                                 cursiv::DocumentId expected) {
    index.updateContent(exporter);
    for (const auto& document : {a, b, c, d})
      CHECK(index.isAffected(*document, {}) == (document->id == expected));
  };

  index.updateReferences(*a);
  CHECK(index.removeReferences(b->id));
  CHECK(index.removeReferences(a->id));
  checkAffected(*x, c->id);
  index.updateReferences(*c);
  checkAffected(*y, d->id);
  index.updateReferences(*d);

  index.updateReferences(*b);
  CHECK(index.removeReferences(c->id));
  CHECK(index.removeReferences(d->id));
  checkAffected(*x, b->id);
  index.updateReferences(*b);
  checkAffected(*y, b->id);
  index.updateReferences(*b);
  CHECK(index.removeReferences(b->id));
  checkAffected(*x, cursiv::kNoDocument);
  checkAffected(*y, cursiv::kNoDocument);
}

// One table, both policies at once: the by-name index is keyed case-folded,
// the query's `caseSensitive` picks the comparison, and every symbol keeps
// the spelling it was exported with. The index itself holds no policy -- it
// is shared by every language of the workspace.
void one_table_answers_both_policies() {
  const World world;
  const auto document = world.built(
      "file:///case.ws",
      "datatype STRING\ndatatype String\nentity Owner { f : String }\n");
  (void)document;
  const cursiv::IndexManager& index = *world.shared.indexManager;
  const cursiv::AstReflection& reflection = *world.ws->astReflection;
  const cursiv::AcceptSet any =
      reflection.referenceAccepts(cursiv::kNoReference);

  // Exact: each spelling answers for itself, and only for itself.
  const cursiv::AstNodeDescription* exact =
      index.findByName("String", true, any, reflection);
  CHECK(exact != nullptr && exact->name == "String");
  exact = index.findByName("STRING", true, any, reflection);
  CHECK(exact != nullptr && exact->name == "STRING");
  CHECK(index.findByName("StrinG", true, any, reflection) == nullptr);

  // Folded: the first spelling in export order answers for the class.
  const cursiv::AstNodeDescription* folded =
      index.findByName("StrinG", false, any, reflection);
  CHECK(folded != nullptr && folded->name == "STRING");

  // The folded visit walks the whole class; the exact one only its spelling.
  std::vector<std::string> names;
  CHECK(index.visitByName("string", false, any, reflection,
                          [&](const cursiv::AstNodeDescription& d) {
                            names.emplace_back(d.name);
                            return true;
                          }));
  CHECK(names.size() == 2);
  CHECK(names[0] == "STRING" && names[1] == "String");
  names.clear();
  CHECK(index.visitByName("string", true, any, reflection,
                          [&](const cursiv::AstNodeDescription& d) {
                            names.emplace_back(d.name);
                            return true;
                          }));
  CHECK(names.empty());
}

// Two languages, two policies, one index: what a case-sensitive language
// cannot see under a misspelt case, an insensitive one resolves -- against
// the very same snapshot.
struct CaseInsensitiveProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;

 protected:
  [[nodiscard]] bool isCaseSensitive(
      const cursiv::ReferenceInfo&) const noexcept override {
    return false;
  }
};

void two_languages_share_the_index() {
  World world;
  // A second language over the same grammar, insensitive where the first
  // keeps the default.
  cursiv::CoreServices* ci = nullptr;
  {
    auto language = std::make_unique<cursiv::CoreServices>(world.shared);
    language->languageMetaData = {"workspace-ci", {".wsci"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    language->scopeProvider =
        std::make_unique<CaseInsensitiveProvider>(*language);
    ci = language.get();
    world.shared.serviceRegistry->registerServices(std::move(language));
  }

  const auto exporter = world.built("file:///lib.ws", "datatype String\n");
  const auto site =
      world.built("file:///use.ws", "entity HasAuthor { author : StrinG }\n");
  (void)exporter;

  const cursiv::ReferenceInfo context =
      static_cast<const cursiv::DefaultAstReflection&>(*world.ws->astReflection)
          .referenceInfo(site->parseResult.references->handles.front());
  CHECK(world.ws->scopeProvider->getScopeEntry(context, "StrinG") == nullptr);
  const cursiv::AstNodeDescription* const found =
      ci->scopeProvider->getScopeEntry(context, "StrinG");
  CHECK(found != nullptr);
  CHECK(found->name == "String");
}

// Only what resolved is described: a reference pointing nowhere makes no row,
// so the inverse table never answers with an edge to nothing.
void only_the_resolved_are_described() {
  const World world;
  const auto document = world.built(
      "file:///a.ws",
      "datatype String\nentity E { a : String }\nentity F { b : Missing }\n");
  const std::vector<cursiv::ReferenceDescription> rows =
      world.ws->referenceDescriptionProvider->createDescriptions(*document, {});
  CHECK(rows.size() == 1);
  CHECK(rows.front().isResolved());
}

}  // namespace

void run() {
  a_type_filter_includes_derived_types();
  homonyms_follow_normalized_uri_order();
  racing_readers_see_the_whole_view();
  readers_fill_a_view_a_writer_retires();
  a_document_exports_its_top_level();
  a_symbol_knows_what_points_at_it();
  removing_a_document_forgets_it();
  only_what_points_at_a_change_is_affected();
  affected_checks_resolve_target_mask_collisions();
  imported_names_follow_reference_removal_and_replacement();
  one_table_answers_both_policies();
  two_languages_share_the_index();
  only_the_resolved_are_described();
}

}  // namespace index_manager

// Pins what one document declares, by container and by name: the order the
// source wrote, and the flat storage `seal` freezes it into.
namespace local_symbols {

namespace {

// A description borrows its name, so a test that made one from a temporary
// would hand the table a dangling view -- which is the whole property being
// pinned. The corpus outlives every table built from it.
std::deque<std::string>& corpus() {
  static std::deque<std::string> kept;
  return kept;
}

cursiv::AstNodeDescription named(std::string name, cursiv::NodeId id) {
  cursiv::AstNodeDescription d;
  d.name = corpus().emplace_back(std::move(name));
  d.documentId = static_cast<cursiv::DocumentId>(1);
  d.symbolId = id;
  return d;
}

// One name's entries at one container, in declaration order.
std::vector<const cursiv::AstNodeDescription*> by_name(
    const cursiv::LocalSymbols& symbols, const cursiv::AstNode* container,
    std::string_view name) {
  std::vector<const cursiv::AstNodeDescription*> found;
  symbols.find(container, name, cursiv::LocalSymbols::hash_name(name))
      .for_each([&](const cursiv::AstNodeDescription& entry) {
        found.push_back(&entry);
        return true;
      });
  return found;
}

void nothing_is_declared_until_something_is() {
  cursiv::LocalSymbols symbols;
  symbols.seal();
  CHECK(symbols.empty());
  CHECK(symbols.empty());
  CHECK(symbols.forContainer(nullptr).empty());
}

// Declaration order is the source's order, whatever the names.
void entries_keep_the_order_the_source_wrote() {
  cursiv::LocalSymbols symbols;
  const auto* const container = reinterpret_cast<const cursiv::AstNode*>(0x10);
  symbols.add(container, named("b", cursiv::NodeId{1}));
  symbols.add(container, named("a", cursiv::NodeId{2}));
  symbols.add(container, named("c", cursiv::NodeId{3}));
  symbols.seal();

  CHECK(!symbols.empty());
  CHECK(symbols.size() == 3);
  const std::span<const cursiv::AstNodeDescription> entries =
      symbols.forContainer(container);
  CHECK(entries.size() == 3);
  CHECK(entries[0].name == "b");
  CHECK(entries[1].name == "a");
  CHECK(entries[2].name == "c");
}

// Same name twice is not an error here: the scope provider decides which wins,
// and it needs them in declaration order to do it.
void a_name_declared_twice_keeps_both_in_order() {
  cursiv::LocalSymbols symbols;
  const auto* const container = reinterpret_cast<const cursiv::AstNode*>(0x10);
  symbols.add(container, named("x", cursiv::NodeId{1}));
  symbols.add(container, named("y", cursiv::NodeId{2}));
  symbols.add(container, named("x", cursiv::NodeId{3}));
  symbols.seal();

  const auto x = by_name(symbols, container, "x");
  CHECK(x.size() == 2);
  CHECK(x[0]->symbolId == cursiv::NodeId{1});
  CHECK(x[1]->symbolId == cursiv::NodeId{3});
  CHECK(by_name(symbols, container, "y").size() == 1);
  CHECK(by_name(symbols, container, "nope").empty());
}

// The name table is built over the sealed entries, so a document large enough
// to have rehashed a map several times still answers in declaration order.
void the_index_survives_growth() {
  cursiv::LocalSymbols symbols;
  const auto* const container = reinterpret_cast<const cursiv::AstNode*>(0x10);
  for (cursiv::NodeId i{0}; i < cursiv::NodeId{512}; ++i)
    symbols.add(container, named("n" + std::to_string(i % 8), i));
  symbols.seal();

  CHECK(symbols.forContainer(container).size() == 512);
  const auto n0 = by_name(symbols, container, "n0");
  CHECK(n0.size() == 64);
  CHECK(n0.front()->symbolId == cursiv::NodeId{0});
  CHECK(n0.front()->name == "n0");
  CHECK(n0.back()->symbolId == cursiv::NodeId{504});
}

// Grouping is by container: two containers declaring one name are two answers,
// and the counting sort keeps each group's own order.
void each_container_owns_its_own() {
  cursiv::LocalSymbols symbols;
  const auto* const a = reinterpret_cast<const cursiv::AstNode*>(0x10);
  const auto* const b = reinterpret_cast<const cursiv::AstNode*>(0x20);
  symbols.add(a, named("x", cursiv::NodeId{1}));
  symbols.add(b, named("x", cursiv::NodeId{2}));
  symbols.seal();

  CHECK(symbols.size() == 2);
  CHECK(by_name(symbols, a, "x")[0]->symbolId == cursiv::NodeId{1});
  CHECK(by_name(symbols, b, "x")[0]->symbolId == cursiv::NodeId{2});
  CHECK(symbols.forContainer(a).size() == 1);
  CHECK(symbols.forContainer(b).size() == 1);

  symbols.clear();
  CHECK(symbols.empty());
  CHECK(symbols.forContainer(a).empty());
}

// `find` answers the folded class whole: two spellings of one name share a
// chain, in declaration order, and an exact reader filters for itself.
void find_folds_the_case_and_keeps_the_order() {
  cursiv::LocalSymbols symbols;
  const auto* const container = reinterpret_cast<const cursiv::AstNode*>(0x10);
  symbols.add(container, named("Foo", cursiv::NodeId{1}));
  symbols.add(container, named("fOO", cursiv::NodeId{2}));
  symbols.add(container, named("Bar", cursiv::NodeId{3}));
  symbols.seal();

  const auto folded = by_name(symbols, container, "foo");
  CHECK(folded.size() == 2);
  CHECK(folded[0]->name == "Foo");
  CHECK(folded[1]->name == "fOO");
  CHECK(by_name(symbols, container, "BAR").size() == 1);
}

// A container whose bit another container set holds nothing: the filter
// only says maybe, and the tables answer. Every address that can share a bit
// with the held ones is asked.
void a_container_sharing_a_bit_holds_nothing() {
  cursiv::LocalSymbols symbols;
  const auto at = [](std::uintptr_t k) {
    return reinterpret_cast<const cursiv::AstNode*>(0x10 + 16 * k);
  };
  for (std::uintptr_t k = 0; k < 8; ++k)
    symbols.add(at(k), named("x", static_cast<cursiv::NodeId>(k)));
  symbols.seal();
  bool nothing = true;
  for (std::uintptr_t k = 8; k < 8 + 4096; ++k)
    nothing = nothing && by_name(symbols, at(k), "x").empty() &&
              symbols.forContainer(at(k)).empty();
  CHECK(nothing);
  for (std::uintptr_t k = 0; k < 8; ++k)
    CHECK(by_name(symbols, at(k), "x").size() == 1);
}

}  // namespace

void run() {
  nothing_is_declared_until_something_is();
  entries_keep_the_order_the_source_wrote();
  a_name_declared_twice_keeps_both_in_order();
  the_index_survives_growth();
  each_container_owns_its_own();
  find_folds_the_case_and_keeps_the_order();
  a_container_sharing_a_bit_holds_nothing();
}

}  // namespace local_symbols

// Pins the folder that is a map: stat/exists over files and synthesized
// folders, the two-level walk, and what removing a file takes back down.
namespace memory_file_system_provider {

namespace {

template <class F>
bool throws(F&& f) {
  try {
    f();
  } catch (const cursiv::FileSystemError&) {
    return true;
  }
  return false;
}

void stat_and_exists_tell_files_from_folders() {
  cursiv::MemoryFileSystemProvider fs;
  fs.addFile("mem:///w/a.txt", "hi");
  fs.addFile("mem:///w/sub/b.txt", "x");

  CHECK(fs.stat("mem:///w/a.txt").isFile);
  CHECK(!fs.stat("mem:///w/a.txt").isDirectory);
  CHECK(fs.stat("mem:///w/sub").isDirectory);
  CHECK(!fs.stat("mem:///w/sub").isFile);
  CHECK(fs.exists("mem:///w/a.txt"));
  CHECK(fs.exists("mem:///w/sub"));
  CHECK(!fs.exists("mem:///w/nope"));
  CHECK(throws([&] { (void)fs.stat("mem:///w/nope"); }));
}

// A key that is itself a file also answers as the root of the deeper keys
// under it: the exact match wins, so it never reports as a folder.
void a_file_can_also_be_a_folders_root() {
  cursiv::MemoryFileSystemProvider fs;
  fs.addFile("mem:///w/x", "leaf");
  fs.addFile("mem:///w/x/y", "child");

  const cursiv::FileSystemNode node = fs.stat("mem:///w/x");
  CHECK(node.isFile);
  CHECK(!node.isDirectory);
  CHECK(fs.readFile("mem:///w/x") == "leaf");
}

// One level only: a deeper key is folded into the folder between, not listed
// itself.
void read_directory_walks_one_level() {
  cursiv::MemoryFileSystemProvider fs;
  fs.addFile("mem:///w/a.txt", "1");
  fs.addFile("mem:///w/sub/b.txt", "2");
  fs.addFile("mem:///w/sub/deep/c.txt", "3");

  const std::vector<cursiv::FileSystemNode> top = fs.readDirectory("mem:///w");
  CHECK(top.size() == 2);
  CHECK(top[0].isFile && top[0].uri == "mem:///w/a.txt");
  CHECK(top[1].isDirectory && top[1].uri == "mem:///w/sub");
  // Trailing slash names the same folder.
  CHECK(fs.readDirectory("mem:///w/").size() == 2);

  const std::vector<cursiv::FileSystemNode> sub =
      fs.readDirectory("mem:///w/sub");
  CHECK(sub.size() == 2);
  CHECK(sub[0].isFile && sub[0].uri == "mem:///w/sub/b.txt");
  CHECK(sub[1].isDirectory && sub[1].uri == "mem:///w/sub/deep");
}

}  // namespace

void run() {
  stat_and_exists_tell_files_from_folders();
  a_file_can_also_be_a_folders_root();
  read_directory_walks_one_level();
}

}  // namespace memory_file_system_provider

// Pins the two units a document is addressed in, and the conversion between
// them: bytes for a parse, lines and UTF-16 columns for a client.
namespace text_document {

namespace {

cursiv::TextDocument doc(std::string text) {
  return cursiv::TextDocument::create("file:///t", "t", 1, std::move(text));
}

void identity_is_readonly() {
  const cursiv::TextDocument d = doc("abc");
  CHECK(d.uri() == "file:///t");
  CHECK(d.languageId() == "t");
  CHECK(d.version() == 1);
  CHECK(d.getText() == "abc");
}

void lines_and_columns() {
  const cursiv::TextDocument d = doc("ab\ncd\n\nef");
  CHECK(d.lineCount() == 4);
  CHECK(d.positionAt(0) == (cursiv::Position{0, 0}));
  CHECK(d.positionAt(1) == (cursiv::Position{0, 1}));
  // The terminator belongs to the line it ends: there is no position inside it.
  CHECK(d.positionAt(2) == (cursiv::Position{0, 2}));
  CHECK(d.positionAt(3) == (cursiv::Position{1, 0}));
  CHECK(d.positionAt(6) == (cursiv::Position{2, 0}));  // the empty line
  CHECK(d.positionAt(7) == (cursiv::Position{3, 0}));
  // Clamped past the end.
  CHECK(d.positionAt(9999) == (cursiv::Position{3, 2}));

  // And back, over everything `positionAt` can produce.
  for (cursiv::TextOffset i = 0; i <= 9; ++i)
    CHECK(d.offsetAt(d.positionAt(i)) == (i > 9 ? 9 : i));
  // A line past the end answers the end; a column past its line, that line's.
  CHECK(d.offsetAt({99, 0}) == 9);
  CHECK(d.offsetAt({1, 99}) == 5);
}

// The column is a UTF-16 code unit, not a byte and not a code point: an emoji
// outside the BMP is a surrogate pair and counts two. Getting this wrong puts
// every diagnostic after it on the wrong column.
// The three terminators of the LSP position model, the pair counting once.
void a_lone_cr_ends_a_line() {
  const cursiv::TextDocument d = doc("a\rb\r\nc\nd");
  CHECK(d.lineCount() == 4);
  CHECK(d.positionAt(2) == (cursiv::Position{1, 0}));
  CHECK(d.positionAt(3) == (cursiv::Position{1, 1}));  // between \r and \n
  CHECK(d.positionAt(5) == (cursiv::Position{2, 0}));
  CHECK(d.positionAt(7) == (cursiv::Position{3, 0}));
  CHECK(d.offsetAt({1, 0}) == 2);
  CHECK(d.offsetAt({2, 0}) == 5);
  CHECK(d.offsetAt({1, 99}) == 3);
}

// What a move leaves behind answers for the text it now holds: nothing.
void a_moved_from_document_forgets_its_lines() {
  cursiv::TextDocument from = doc("ab\ncd");
  CHECK(from.lineCount() == 2);
  const cursiv::TextDocument to = std::move(from);
  CHECK(to.lineCount() == 2);
  CHECK(from.getText().empty());  // NOLINT(bugprone-use-after-move)
  CHECK(from.lineCount() == 1);
  CHECK(from.positionAt(5) == (cursiv::Position{0, 0}));
}

void columns_count_utf16_units() {
  const cursiv::TextDocument d =
      doc("a\xC3\xA9\xF0\x9F\x98\x80"
          "b");
  // a(1 byte) e-acute(2 bytes, 1 unit) emoji(4 bytes, 2 units) b
  CHECK(d.positionAt(0) == (cursiv::Position{0, 0}));
  CHECK(d.positionAt(1) == (cursiv::Position{0, 1}));  // after 'a'
  CHECK(d.positionAt(3) == (cursiv::Position{0, 2}));  // after the e-acute
  CHECK(d.positionAt(7) == (cursiv::Position{0, 4}));  // after the emoji: +2
  CHECK(d.offsetAt({0, 4}) == 7);
  CHECK(d.offsetAt({0, 2}) == 3);
}

void a_range_is_a_pair() {
  const cursiv::TextDocument d = doc("ab\ncd");
  CHECK(d.rangeAt(1, 4) == (cursiv::Range{{0, 1}, {1, 1}}));
  // `end` before `begin` is an empty range at `begin`.
  CHECK(d.rangeAt(4, 1) == (cursiv::Range{{1, 1}, {1, 1}}));
  CHECK(d.getText({{0, 1}, {1, 1}}) == "b\nc");
  CHECK(d.getText({{1, 1}, {0, 1}}) == "b\nc");
}

void changes_apply_in_order() {
  cursiv::TextDocument d = doc("hello world");
  const std::vector<cursiv::TextDocumentContentChangeEvent> changes = {
      {cursiv::Range{{0, 0}, {0, 5}}, "goodbye"},
      // Written against the text the first change left.
      {cursiv::Range{{0, 8}, {0, 13}}, "there"},
  };
  cursiv::TextDocument::update(d, changes, 7);
  CHECK(d.getText() == "goodbye there");
  CHECK(d.version() == 7);

  // A change with no range replaces the whole text.
  const std::vector<cursiv::TextDocumentContentChangeEvent> whole = {
      {std::nullopt, "reset"}};
  cursiv::TextDocument::update(d, whole, 8);
  CHECK(d.getText() == "reset");
}

// Edits are the other way round: they are all written against the same text,
// so applying them front to back would move the ground under the later ones.
void edits_leave_the_document_alone() {
  const cursiv::TextDocument d = doc("one two three");
  const std::vector<cursiv::TextEdit> edits = {
      {{{0, 0}, {0, 3}}, "1"},
      {{{0, 8}, {0, 13}}, "3"},
  };
  CHECK(cursiv::TextDocument::applyEdits(d, edits) == "1 two 3");
  CHECK(d.getText() == "one two three");  // untouched
}

// Two insertions at the same point land in array order.
void equal_starts_keep_the_array_order() {
  const cursiv::TextDocument d = doc("ab");
  const std::vector<cursiv::TextEdit> edits = {
      {{{0, 1}, {0, 1}}, "X"},
      {{{0, 1}, {0, 1}}, "Y"},
  };
  CHECK(cursiv::TextDocument::applyEdits(d, edits) == "aXYb");
}

// An inverted range is straightened -- a replacement, never an insertion.
void an_inverted_range_is_straightened() {
  const cursiv::TextDocument d = doc("abcd");
  const std::vector<cursiv::TextEdit> edits = {{{{0, 3}, {0, 1}}, "Z"}};
  CHECK(cursiv::TextDocument::applyEdits(d, edits) == "aZd");
}

void edits_are_sorted_after_normalizing_their_ranges() {
  const cursiv::TextDocument d = doc("0123456789");
  const std::vector<cursiv::TextEdit> edits = {
      {{{0, 8}, {0, 8}}, "I"},
      {{{0, 8}, {0, 6}}, "R"},
  };
  CHECK(cursiv::TextDocument::applyEdits(d, edits) == "012345RI89");
  const std::vector<cursiv::TextEdit> clamped = {
      {{{0, 99}, {0, 99}}, "A"},
      {{{0, 10}, {0, 10}}, "B"},
  };
  CHECK(cursiv::TextDocument::applyEdits(d, clamped) == "0123456789BA");
}

// Overlapping edits are refused, never silently merged into corrupt text.
void overlapping_edits_are_refused() {
  const cursiv::TextDocument d = doc("abcdef");
  const std::vector<cursiv::TextEdit> edits = {
      {{{0, 0}, {0, 3}}, "X"},
      {{{0, 2}, {0, 5}}, "Y"},
  };
  bool thrown = false;
  try {
    (void)cursiv::TextDocument::applyEdits(d, edits);
  } catch (const std::invalid_argument&) {
    thrown = true;
  }
  CHECK(thrown);
  // Touching is legal: one edit may end where the next starts.
  const std::vector<cursiv::TextEdit> touching = {
      {{{0, 0}, {0, 2}}, "X"},
      {{{0, 2}, {0, 4}}, "Y"},
  };
  CHECK(cursiv::TextDocument::applyEdits(d, touching) == "XYef");
}

// The column of a line that holds no lead byte is its byte column, and the
// index says so per line: a line with an accent on it still counts its units,
// and a line whose accent stands past the offset counts them too.
void an_ascii_line_answers_in_bytes() {
  const cursiv::TextDocument d =
      doc("plain\nca\xC3\xA7"
          "a\nplain\n\xC3\xA9nd");
  CHECK(d.lineCount() == 4);
  CHECK(d.positionAt(5) == (cursiv::Position{0, 5}));
  // `ca`, the c-cedilla (one unit for two bytes), then `a`.
  CHECK(d.positionAt(11) == (cursiv::Position{1, 4}));
  CHECK(d.offsetAt({1, 4}) == 11);
  CHECK(d.offsetAt({1, 2}) == 8);
  CHECK(d.positionAt(17) == (cursiv::Position{2, 5}));
  CHECK(d.positionAt(21) == (cursiv::Position{3, 2}));
}

// The scan reads eight bytes at a time, so a terminator on a word's last byte
// and its partner on the next word's first are the pair that must count once.
void a_pair_across_a_word_still_counts_once() {
  //                    0123456 7 8
  const cursiv::TextDocument d = doc("abcdefg\r\nhij");
  CHECK(d.lineCount() == 2);
  CHECK(d.positionAt(7) == (cursiv::Position{0, 7}));
  CHECK(d.positionAt(8) == (cursiv::Position{0, 7}));  // inside the pair
  CHECK(d.positionAt(9) == (cursiv::Position{1, 0}));
  const cursiv::TextDocument e = doc("abcdefgh\r\nij");
  CHECK(e.lineCount() == 2);
  CHECK(e.positionAt(10) == (cursiv::Position{1, 0}));
  // A lone byte over 0x7F on the far side of a word boundary marks its line.
  const cursiv::TextDocument f = doc("abcdefgh\xC3\xA9j\nk");
  CHECK(f.positionAt(11) == (cursiv::Position{0, 10}));
  CHECK(f.positionAt(12) == (cursiv::Position{1, 0}));
}

// One bit per line: the word the bits live in fills at 64, and the line that
// crosses the boundary is the one to ask.
void the_flag_holds_past_sixty_four_lines() {
  std::string text;
  for (int i = 0; i < 70; ++i) text += i == 65 ? "\xC3\xA9x\n" : "ab\n";
  const cursiv::TextDocument d = doc(text);
  CHECK(d.lineCount() == 71);
  CHECK(d.positionAt(cursiv::TextOffset(64 * 3 + 2)) ==
        (cursiv::Position{64, 2}));
  // Line 65 is `<e-acute>x`: three bytes, two units.
  CHECK(d.positionAt(cursiv::TextOffset(65 * 3 + 3)) ==
        (cursiv::Position{65, 2}));
  CHECK(d.offsetAt({65, 2}) == 65 * 3 + 3);
  CHECK(d.positionAt(cursiv::TextOffset(65 * 3 + 4 + 2)) ==
        (cursiv::Position{66, 2}));
}

}  // namespace

// Both conversions count eight bytes at a stroke where the word ends on a
// boundary; a walk that reads one byte at a time is what says they agree.
void the_word_count_answers_what_a_byte_walk_answers() {
  constexpr std::string_view kPieces[] = {
      "a",  "z",    "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", " ",
      "\n", "\r\n", "\r"};
  const auto units = [](std::string_view text, std::size_t from,
                        std::size_t to) {
    std::uint32_t n = 0;
    for (std::size_t at = from; at < to;) {
      const std::size_t step = cursiv::utf8_width(text[at]);
      n += step == 4 ? 2U : 1U;
      at += step;
    }
    return n;
  };
  for (unsigned seed = 1; seed <= 200; ++seed) {
    std::string text;
    unsigned x = seed;
    for (int i = 0; i < 40; ++i) {
      x = (x * 1103515245U) + 12345U;
      text += kPieces[(x >> 16U) % 9];
    }
    const cursiv::TextDocument d =
        cursiv::TextDocument::create("file:///a", "t", 1, text);
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < text.size();) {
      if (text[i] == '\r')
        starts.push_back(i +=
                         (i + 1 < text.size() && text[i + 1] == '\n') ? 2 : 1);
      else if (text[i] == '\n')
        starts.push_back(++i);
      else
        ++i;
    }
    for (std::size_t off = 0; off <= text.size(); ++off) {
      if (off < text.size() &&
          (static_cast<unsigned char>(text[off]) & 0xC0U) == 0x80U)
        continue;
      std::size_t line = 0;
      for (std::size_t i = 0; i < starts.size(); ++i)
        if (starts[i] <= off) line = i;
      std::size_t end =
          line + 1 < starts.size() ? starts[line + 1] : text.size();
      while (end > starts[line] &&
             (text[end - 1] == '\n' || text[end - 1] == '\r'))
        --end;
      const std::size_t clamped = off > end ? end : off;
      const cursiv::Position at =
          d.positionAt(static_cast<cursiv::TextOffset>(off));
      CHECK(at.line == line);
      CHECK(at.character == units(text, starts[line], clamped));
      CHECK(d.offsetAt(at) == clamped);
    }
  }
}

// A text that is not UTF-8 -- a Latin-1 file read from disk -- counts as an
// editor decoding it with replacement shows it: each maximal ill-formed
// subpart is one U+FFFD, one unit. The column never goes back.
void an_ill_formed_subpart_is_one_replacement() {
  const cursiv::TextDocument stray = doc("\xB0\xB0" "abcdefgh");
  CHECK(stray.positionAt(7).character == 7);
  CHECK(stray.positionAt(8).character == 8);
  const cursiv::Range r = stray.rangeAt(7, 8);
  CHECK(r.start.character <= r.end.character);
  CHECK(stray.offsetAt({0, 8}) == 8);
  // A Latin-1 e-acute reads as a lead whose sequence never comes.
  const cursiv::TextDocument latin1 = doc("\xE9" "abcdefghij");
  CHECK(latin1.positionAt(8).character == 8);
  CHECK(latin1.offsetAt({0, 1}) == 1);
  // A well-formed prefix cut short is one subpart, not one per byte.
  CHECK(doc("\xE2\x82" "A").positionAt(3).character == 2);
  // Overlong, surrogate and out-of-range forms are a subpart per byte.
  CHECK(doc("\xC0\x80").positionAt(2).character == 2);
  CHECK(doc("\xED\xA0\x80").positionAt(3).character == 3);
  CHECK(doc("\xF4\x90\x80\x80").positionAt(4).character == 4);
  CHECK(doc("\xF5z").positionAt(2).character == 2);
}

// A decoder written from the Encoding Standard's UTF-8 decoder, one item per
// scalar or U+FFFD: where it starts, how many bytes, how many units.
struct Item {
  std::size_t start;
  std::size_t units;
};

std::vector<Item> replacing_decode(std::string_view text) {
  std::vector<Item> items;
  std::size_t needed = 0, seen = 0, start = 0;
  unsigned lower = 0x80, upper = 0xBF;
  for (std::size_t i = 0; i < text.size();) {
    const auto b = static_cast<unsigned char>(text[i]);
    if (needed == 0) {
      start = i;
      if (b <= 0x7F) {
        items.push_back({i, 1});
      } else if (b >= 0xC2 && b <= 0xDF) {
        needed = 1;
      } else if (b >= 0xE0 && b <= 0xEF) {
        if (b == 0xE0) lower = 0xA0;
        if (b == 0xED) upper = 0x9F;
        needed = 2;
      } else if (b >= 0xF0 && b <= 0xF4) {
        if (b == 0xF0) lower = 0x90;
        if (b == 0xF4) upper = 0x8F;
        needed = 3;
      } else {
        items.push_back({i, 1});
      }
      ++i;
      continue;
    }
    if (b < lower || b > upper) {
      // The sequence so far is one replacement; this byte is read again.
      items.push_back({start, 1});
      needed = seen = 0;
      lower = 0x80;
      upper = 0xBF;
      continue;
    }
    lower = 0x80;
    upper = 0xBF;
    ++i;
    if (++seen == needed) {
      items.push_back({start, needed == 3 ? 2u : 1u});
      needed = seen = 0;
    }
  }
  if (needed != 0) items.push_back({start, 1});
  return items;
}

// Every offset of random lines -- well-formed runs long enough for the word
// count, stray and truncated bytes between them -- against that decoder, and
// every column back.
void ill_formed_text_answers_what_a_replacing_decoder_answers() {
  constexpr std::string_view kPieces[] = {
      "abcdefgh", "a",   "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
      "\n",       "\r\n", "\x80",     "\xbf",         "\xc3",
      "\xe2\x82",  "\xf0\x9f\x98", "\xc0\x80", "\xed\xa0\x80", "\xf5",
      "\xff"};
  constexpr std::size_t kCount = std::size(kPieces);
  unsigned x = 7;
  for (int round = 0; round < 1500; ++round) {
    std::string text;
    const int pieces = 1 + static_cast<int>((x >> 8U) % 24);
    for (int i = 0; i < pieces; ++i) {
      x = (x * 1103515245U) + 12345U;
      text += kPieces[(x >> 16U) % kCount];
    }
    const cursiv::TextDocument d = doc(text);
    const std::vector<Item> items = replacing_decode(text);
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < text.size();) {
      if (text[i] == '\r')
        starts.push_back(i +=
                         (i + 1 < text.size() && text[i + 1] == '\n') ? 2 : 1);
      else if (text[i] == '\n')
        starts.push_back(++i);
      else
        ++i;
    }
    std::uint32_t last = 0;
    std::size_t lastLine = 0;
    for (std::size_t off = 0; off <= text.size(); ++off) {
      std::size_t line = 0;
      for (std::size_t i = 0; i < starts.size(); ++i)
        if (starts[i] <= off) line = i;
      std::size_t end =
          line + 1 < starts.size() ? starts[line + 1] : text.size();
      while (end > starts[line] &&
             (text[end - 1] == '\n' || text[end - 1] == '\r'))
        --end;
      const std::size_t clamped = off > end ? end : off;
      std::uint32_t want = 0;
      for (const Item& item : items)
        if (item.start >= starts[line] && item.start < clamped)
          want += static_cast<std::uint32_t>(item.units);
      const cursiv::Position at =
          d.positionAt(static_cast<cursiv::TextOffset>(off));
      CHECK(at.line == line);
      CHECK(at.character == want);
      if (line == lastLine) CHECK(at.character >= last);
      last = at.character;
      lastLine = line;
    }
    // Back: a column lands on the first item that reaches it.
    for (std::size_t line = 0; line < starts.size(); ++line) {
      std::size_t end =
          line + 1 < starts.size() ? starts[line + 1] : text.size();
      while (end > starts[line] &&
             (text[end - 1] == '\n' || text[end - 1] == '\r'))
        --end;
      std::uint32_t total = 0;
      std::vector<std::pair<std::size_t, std::uint32_t>> bounds;
      for (const Item& item : items)
        if (item.start >= starts[line] && item.start < end) {
          bounds.emplace_back(item.start, total);
          total += static_cast<std::uint32_t>(item.units);
        }
      for (std::uint32_t column = 0; column <= total + 1; ++column) {
        std::size_t want = end;
        for (const auto& [start, before] : bounds)
          if (before >= column) {
            want = start;
            break;
          }
        CHECK(d.offsetAt({static_cast<std::uint32_t>(line), column}) == want);
      }
    }
  }
}

void run() {
  identity_is_readonly();
  lines_and_columns();
  a_lone_cr_ends_a_line();
  a_moved_from_document_forgets_its_lines();
  columns_count_utf16_units();
  an_ascii_line_answers_in_bytes();
  a_pair_across_a_word_still_counts_once();
  the_flag_holds_past_sixty_four_lines();
  the_word_count_answers_what_a_byte_walk_answers();
  an_ill_formed_subpart_is_one_replacement();
  ill_formed_text_answers_what_a_replacing_decoder_answers();
  a_range_is_a_pair();
  changes_apply_in_order();
  edits_leave_the_document_alone();
  equal_starts_keep_the_array_order();
  an_inverted_range_is_straightened();
  edits_are_sorted_after_normalizing_their_ranges();
  overlapping_edits_are_refused();
}

}  // namespace text_document

// Pins the workspace scan: the folders' claimed files reach the store built,
// the filter keeps what a language claims, the additional-documents hook runs
// before any folder, and `ready` resolves once the documents are in.
namespace workspace_manager {

namespace {

// A manager with a library of its own: what the hook contract feeds.
class LibraryWorkspaceManager final : public cursiv::DefaultWorkspaceManager {
 public:
  using DefaultWorkspaceManager::DefaultWorkspaceManager;

 protected:
  void loadAdditionalDocuments(
      std::span<const cursiv::WorkspaceFolder>,
      cursiv::function_ref<void(std::string_view, std::string)> add) override {
    add("mem:///lib.ws", "datatype Library\n");
  }
};

struct World {
  cursiv::SharedCoreServices shared;
  cursiv::MemoryFileSystemProvider* disk = nullptr;

  explicit World(bool withLibrary = false) {
    auto memory = std::make_unique<cursiv::MemoryFileSystemProvider>();
    disk = memory.get();
    shared.fileSystemProvider = std::move(memory);
    if (withLibrary)
      shared.workspaceManager =
          std::make_unique<LibraryWorkspaceManager>(shared);
    cursiv::install_default_shared_core_services(shared);
    auto language = std::make_unique<cursiv::CoreServices>(shared);
    language->languageMetaData = {"workspace", {".ws"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    shared.serviceRegistry->registerServices(std::move(language));
  }
};

void the_scan_builds_what_the_folders_hold() {
  World world;
  world.disk->addFile("file:///w/a.ws", "datatype A\n");
  world.disk->addFile("file:///w/sub/b.ws", "datatype B\n");
  world.disk->addFile("file:///w/note.txt", "not ours\n");
  const cursiv::WorkspaceFolder folders[] = {{"file:///w", "w"}};
  world.shared.workspaceManager->initializeWorkspace(folders);

  const cursiv::Documents& store = *world.shared.documents;
  const auto a = store.getDocument("file:///w/a.ws");
  const auto b = store.getDocument("file:///w/sub/b.ws");
  CHECK(a != nullptr && b != nullptr);
  CHECK(a->state.load() == cursiv::DocumentState::Validated);
  CHECK(b->state.load() == cursiv::DocumentState::Validated);
  CHECK(store.getDocument("file:///w/note.txt") == nullptr);  // unclaimed
  CHECK(world.shared.workspaceManager->workspaceFolders().size() == 1);

  // Resolved once the documents are in: no wait left.
  const std::shared_future<void> ready = world.shared.workspaceManager->ready();
  CHECK(ready.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
}

void an_unclaimed_file_kept_by_a_custom_filter_is_skipped() {
  struct IncludeAll final : cursiv::DefaultWorkspaceManager {
    using DefaultWorkspaceManager::DefaultWorkspaceManager;
    bool shouldIncludeEntry(const cursiv::FileSystemNode&) const override {
      return true;
    }
  };
  World world;
  world.shared.workspaceManager = std::make_unique<IncludeAll>(world.shared);
  world.disk->addFile("file:///w/a.ws", "datatype A\n");
  world.disk->addFile("file:///w/unknown.txt", "not a language");
  const cursiv::WorkspaceFolder folders[] = {{"file:///w", "w"}};
  world.shared.workspaceManager->initializeWorkspace(folders);
  CHECK(world.shared.documents->all().size() == 1);
  CHECK(world.shared.documents->hasDocument("file:///w/a.ws"));
}

void the_filter_keeps_what_a_language_claims() {
  World world;
  const cursiv::WorkspaceManager& manager = *world.shared.workspaceManager;
  CHECK(manager.shouldIncludeEntry(
      {.isFile = true, .isDirectory = false, .uri = "file:///w/a.ws"}));
  CHECK(!manager.shouldIncludeEntry(
      {.isFile = true, .isDirectory = false, .uri = "file:///w/a.txt"}));
  // A hidden entry is refused, file and directory alike.
  CHECK(!manager.shouldIncludeEntry(
      {.isFile = false, .isDirectory = true, .uri = "file:///w/.git"}));
}

// The memory provider keys a file as `normalize_uri` spells it: added under
// an upper-case scheme, it reads back under either case, and a workspace
// opened on the same upper-case folder finds it.
void an_upper_case_scheme_names_the_same_file() {
  World world;
  world.disk->addFile("MEM:///w/a.ws", "datatype A\n");
  CHECK(world.disk->readFile("MEM:///w/a.ws") == "datatype A\n");
  CHECK(world.disk->readFile("mem:///w/a.ws") == "datatype A\n");
  const cursiv::WorkspaceFolder folders[] = {{"MEM:///w", "w"}};
  world.shared.workspaceManager->initializeWorkspace(folders);
  const auto a = world.shared.documents->getDocument("MEM:///w/a.ws");
  CHECK(a != nullptr && a->state.load() == cursiv::DocumentState::Validated);
}

// A root narrower than its workspace folder: an entry inside the folder but
// outside the root is one the scan never reaches, so it is not included. Only
// an entry no workspace folder holds is judged on its own.
void an_entry_outside_the_root_of_its_folder_is_not_included() {
  struct SourceRoot final : cursiv::DefaultWorkspaceManager {
    using DefaultWorkspaceManager::DefaultWorkspaceManager;

   protected:
    std::string getRootFolder(
        const cursiv::WorkspaceFolder& folder) const override {
      return cursiv::normalize_uri(folder.uri) + "/src";
    }
  };
  World world;
  world.shared.workspaceManager = std::make_unique<SourceRoot>(world.shared);
  world.disk->addFile("mem:///w/src/a.ws", "datatype A\n");
  world.disk->addFile("mem:///w/docs/b.ws", "datatype B\n");
  const cursiv::WorkspaceFolder folders[] = {{"mem:///w", "w"}};
  world.shared.workspaceManager->initializeWorkspace(folders);
  CHECK(world.shared.documents->hasDocument("mem:///w/src/a.ws"));
  CHECK(!world.shared.documents->hasDocument("mem:///w/docs/b.ws"));

  const cursiv::WorkspaceManager& manager = *world.shared.workspaceManager;
  const auto file = [](std::string uri) {
    return cursiv::FileSystemNode{
        .isFile = true, .isDirectory = false, .uri = std::move(uri)};
  };
  CHECK(manager.isIncluded(file("mem:///w/src/a.ws")));
  CHECK(!manager.isIncluded(file("mem:///w/docs/b.ws")));
  CHECK(manager.isIncluded(file("mem:///elsewhere/c.ws")));
}

// The hook's documents are in the store and built, workspace files beside
// them, even when the folders hold nothing at all.
void additional_documents_load_even_without_folders() {
  World world(/*withLibrary=*/true);
  world.shared.workspaceManager->initializeWorkspace({});
  const auto library = world.shared.documents->getDocument("mem:///lib.ws");
  CHECK(library != nullptr);
  CHECK(library->state.load() == cursiv::DocumentState::Validated);
}

void the_scan_relinks_an_open_document_built_before_startup() {
  World world;
  const auto document = world.shared.documents->createDocument(
      "file:///w/open.ws", "entity E { f : Library }\n");
  document->isOpen = true;
  cursiv::Document* documents[] = {document.get()};
  world.shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
    world.shared.documentBuilder->build(documents, {}, token);
  });
  CHECK(document->hasLinkingErrors());
  CHECK(!document->diagnostics.empty());
  const auto* root = document->parseResult.value;
  const auto revision = document->buildRevision;

  world.disk->addFile("file:///w/library.ws", "datatype Library\n");
  const cursiv::WorkspaceFolder folders[] = {{"file:///w", "w"}};
  world.shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
    world.shared.workspaceManager->initializeWorkspace(folders, token);
  });
  CHECK(!document->hasLinkingErrors());
  CHECK(document->diagnostics.empty());
  CHECK(document->state.load() == cursiv::DocumentState::Validated);
  CHECK(document->parseResult.value == root);
  CHECK(document->buildRevision == revision);
}

void a_scan_updates_existing_links_and_keeps_the_requested_options() {
  World world;
  world.disk->addFile("file:///w/b.ws", "datatype Same");
  world.disk->addFile("file:///w/use.ws", "entity E { f : Same }");
  const cursiv::WorkspaceFolder folders[] = {{"file:///w", "w"}};
  world.shared.workspaceManager->initializeWorkspace(folders);
  const auto use = world.shared.documents->getDocument("file:///w/use.ws");
  const auto* root = use->parseResult.value;
  const auto revision = use->buildRevision;
  const auto target = [&] {
    const auto* entity = cursiv::ast_cast<ws::ast::Entity>(
        use->parseResult.value_as<ws::ast::Workspace>()->elements[0]);
    const auto* resolved = entity->features[0]->type.get();
    return resolved == nullptr ? std::string{} : resolved->getDocument()->uri;
  };
  CHECK(target() == "file:///w/b.ws");
  world.disk->addFile("file:///w/a.ws", "datatype Same");
  world.shared.workspaceManager->initialBuildOptions() = {false, {}};
  world.shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
    world.shared.workspaceManager->initializeWorkspace(folders, token);
  });
  CHECK(target() == "file:///w/a.ws");
  CHECK(use->parseResult.value == root);
  CHECK(use->buildRevision == revision);
  CHECK(use->state.load() == cursiv::DocumentState::IndexedReferences);
}

// The option set before the first scan governs the first build: a broken
// reference publishes nothing when the scan was asked not to validate.
void the_initial_options_govern_the_first_build() {
  World world;
  world.disk->addFile("file:///w/a.ws", "entity E { f : Missing }\n");
  world.shared.workspaceManager->initialBuildOptions() =
      cursiv::BuildOptions{false, {}};
  const cursiv::WorkspaceFolder folders[] = {{"file:///w", "w"}};
  world.shared.workspaceManager->initializeWorkspace(folders);

  const auto a = world.shared.documents->getDocument("file:///w/a.ws");
  CHECK(a != nullptr);
  CHECK(a->state.load() == cursiv::DocumentState::IndexedReferences);
  CHECK(a->diagnostics.empty());  // asked for none, published none
}

// `ready` resolves whatever the scan met, a hook that throws included: the
// failure still reaches the caller, and nobody awaiting the scan is left
// waiting for the session.
void ready_resolves_when_the_scan_throws() {
  struct Failing final : cursiv::DefaultWorkspaceManager {
    using DefaultWorkspaceManager::DefaultWorkspaceManager;

   protected:
    void loadAdditionalDocuments(
        std::span<const cursiv::WorkspaceFolder>,
        cursiv::function_ref<void(std::string_view, std::string)>) override {
      throw std::runtime_error("the library is gone");
    }
  };
  World world;
  world.shared.workspaceManager = std::make_unique<Failing>(world.shared);
  bool thrown = false;
  try {
    world.shared.workspaceManager->initializeWorkspace({});
  } catch (const std::runtime_error&) {
    thrown = true;
  }
  CHECK(thrown);
  CHECK(world.shared.workspaceManager->ready().wait_for(
            std::chrono::seconds(0)) == std::future_status::ready);
}

}  // namespace

void run() {
  the_scan_builds_what_the_folders_hold();
  the_filter_keeps_what_a_language_claims();
  an_upper_case_scheme_names_the_same_file();
  an_entry_outside_the_root_of_its_folder_is_not_included();
  an_unclaimed_file_kept_by_a_custom_filter_is_skipped();
  additional_documents_load_even_without_folders();
  the_scan_relinks_an_open_document_built_before_startup();
  a_scan_updates_existing_links_and_keeps_the_requested_options();
  the_initial_options_govern_the_first_build();
  ready_resolves_when_the_scan_throws();
}

}  // namespace workspace_manager

int main() {
  ast_node_locator::run();
  document_builder::run();
  document_factory::run();
  document::run();
  documents::run();
  file_system_provider::run();
  index_manager::run();
  local_symbols::run();
  memory_file_system_provider::run();
  text_document::run();
  workspace_manager::run();
  return cursiv::test::report("workspace");
}
