// Names and what they reach: the linker, the name provider, references across
// documents, and the scopes computed and provided.

#include "cursiv/core/references/References.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/references/NarrowedSiteGrammar.hpp"
#include "core/references/SharedRecordGrammar.hpp"
#include "cursiv/core/execution/TaskScheduler.hpp"
#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/DefaultLinker.hpp"
#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/references/DefaultScopeComputation.hpp"
#include "cursiv/core/references/DefaultScopeProvider.hpp"
#include "cursiv/core/references/LinkingError.hpp"
#include "cursiv/core/references/LinkingResult.hpp"
#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/DefaultAstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/validation/DefaultDocumentValidator.hpp"
#include "cursiv/core/workspace/DefaultDocumentFactory.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/ExpectedFindReferences.hpp"
#include "languages/multiref/MultiRefGrammar.hpp"
#include "multiref/generated/MultiG/AstReflection.hpp"
#include "multiref/generated/MultiG/Module.hpp"
#include "narrowedsite/generated/NarrowedSite/Module.hpp"
#include "sharedrecord/generated/SharedRecord/Module.hpp"
#include "workspace/core/ast.hpp"
#include "workspace/generated/Workspace/AstReflection.hpp"
#include "workspace/generated/Workspace/Module.hpp"

// Pins what a name points at. Reading a reference resolves it -- the lazy
// `ref` -- and the linker is what the read reaches.
namespace linker {

namespace {

// One reference site, filled by hand, which is what `ReferenceInfo` is for: a
// content assist has no reference object either. The columns come off the
// generated metadata, indexed by the handle's own row.
cursiv::ReferenceInfo site_of(const cursiv::AstMetaData& meta,
                              const cursiv::RefHandle& h,
                              std::string_view text) {
  return {.container = h.owner,
          .referenceText = text,
          .property = meta.fields[h.field].name,
          .index = h.index,
          .reference = h.field,
          .narrow = meta.refOps[h.field].narrow_of(*h.owner, h.index)};
}

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
    language->astReflection =
        ws::generated::Workspace::create_ast_reflection();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    cursiv::install_default_core_services(*language);
    ws = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }

  std::shared_ptr<cursiv::Document> scoped(std::string text) const {
    auto document = shared.documentFactory->fromString(std::move(text),
                                                       "file:///w/a.ws");
    shared.documents->addDocument(document);
    document->localSymbols =
        ws->scopeComputation->collectLocalSymbols(*document);
    document->state = cursiv::DocumentState::ComputedScopes;
    return document;
  }

  // The first feature of the first entity -- the one reference the fixture has.
  // The owner is what the work list names, so no walk and no cast is needed.
  static ws::ast::Feature& feature(const cursiv::Document& document) {
    return *static_cast<ws::ast::Feature*>(
        document.parseResult.references->handles.front().owner);
  }
};

void linking_results_distinguish_null_from_an_error() {
  const cursiv::AstNodeDescriptionOrError empty = nullptr;
  CHECK(empty.has_value() && *empty == nullptr);
  cursiv::ReferenceInfo site{};
  site.referenceText = "Absent";
  const cursiv::AstNodeDescriptionOrError refused =
      cursiv::LinkingError{"missing", site};
  CHECK(!refused.has_value());
  CHECK(refused.error().message == "missing");
  CHECK(refused.error().info.referenceText == "Absent");
  bool checked = false;
  try {
    (void)*refused;
  } catch (const std::bad_variant_access&) {
    checked = true;
  }
  CHECK(checked);

  cursiv::AstNodeDescriptionsOrError plural =
      std::vector<const cursiv::AstNodeDescription*>{nullptr};
  const auto* const storage = plural->data();
  const std::vector<const cursiv::AstNodeDescription*> moved = *std::move(plural);
  CHECK(moved.size() == 1 && moved.front() == nullptr);
  CHECK(moved.data() == storage);
}

void a_name_points_at_what_it_declares() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  world.ws->linker->link(*document);

  ws::ast::Feature& age = World::feature(*document);
  const ws::ast::Type* const target = age.type.get();
  CHECK(target != nullptr);
  CHECK(world.ws->nameProvider->getName(*target) ==
        std::optional<std::string_view>("String"));
}

// The read is what resolves, which is the whole point of the home: nothing
// forced this one.
void reading_a_reference_resolves_it() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  // No `link` call at all -- only the linker planted, as a build would.
  document->parseResult.references->linker = world.ws->linker.get();

  ws::ast::Feature& age = World::feature(*document);
  CHECK(age.type.get() != nullptr);
}

// A read made while the document is below `ComputedScopes` fails and is not
// kept: once the scopes are computed, the same reference resolves.
void an_early_read_is_read_again() {
  const World world;
  const auto document = world.shared.documentFactory->fromString(
      "datatype String\nentity Person { age : String }\n", "file:///w/a.ws");
  world.shared.documents->addDocument(document);
  ws::ast::Feature& age = World::feature(*document);
  // Before any linker is planted, then with one planted below the rung.
  CHECK(age.type.get() == nullptr);
  CHECK(age.type.state() == cursiv::RefState::Unresolved);
  world.ws->linker->link(*document);
  CHECK(age.type.state() == cursiv::RefState::Unresolved);

  document->localSymbols =
      world.ws->scopeComputation->collectLocalSymbols(*document);
  document->state = cursiv::DocumentState::ComputedScopes;
  world.ws->linker->link(*document);
  CHECK(age.type.get() != nullptr);
}

// A name nothing declares stays unresolved, and says so rather than lying.
void an_unknown_name_resolves_to_nothing() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : Absent }\n");
  world.ws->linker->link(*document);
  CHECK(World::feature(*document).type.get() == nullptr);
}

// `unlink` clears every answer, and it is the one place that loop is written.
void unlink_forgets_every_answer() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  world.ws->linker->link(*document);
  CHECK(World::feature(*document).type.get() != nullptr);

  world.ws->linker->unlink(*document);
  // Nobody answers now, so the read fails rather than handing back a stale
  // target.
  CHECK(World::feature(*document).type.get() == nullptr);
}

// The primitive under it all, asked directly.
void the_candidate_is_the_scope_s_first_fit() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  const cursiv::ReferenceInfo site =
      site_of(ws::generated::Workspace::kAstMetaData,
              document->parseResult.references->handles.front(), "String");

  const cursiv::AstNodeDescriptionOrError found =
      world.ws->linker->getCandidate(site);
  CHECK(found.has_value());
  CHECK((*found)->name == "String");

  // A site that spells nothing is an error, not an empty scope -- worded like
  // any failed lookup, so one override rewords both.
  cursiv::ReferenceInfo nameless = site;
  nameless.referenceText = {};
  const auto refused = world.ws->linker->getCandidate(nameless);
  CHECK(!refused.has_value());
  CHECK(refused.error().message ==
        "Could not resolve reference to Type named ''.");
}

void plural_candidates_keep_first_identity_in_scope_order() {
  World world;
  struct Repeated final : cursiv::DefaultScopeProvider {
    using DefaultScopeProvider::DefaultScopeProvider;
    using DefaultScopeProvider::visitScopeEntries;
    std::vector<cursiv::AstNodeDescription> entries;
    bool visitScopeEntries(
        const cursiv::ReferenceInfo&, std::string_view,
        cursiv::function_ref<bool(const cursiv::AstNodeDescription&)> visitor)
        const override {
      for (const auto& entry : entries)
        if (!visitor(entry)) return false;
      return true;
    }
  };
  auto scope = std::make_unique<Repeated>(*world.ws);
  Repeated* rows = scope.get();
  for (unsigned i = 0; i < 1024; ++i)
    rows->entries.push_back({.name = "Same",
                             .documentId = cursiv::DocumentId{i % 2},
                             .symbolId = i / 2});
  for (unsigned i = 0; i < 1024; ++i)
    rows->entries.push_back(rows->entries[1023 - i]);
  world.ws->scopeProvider = std::move(scope);
  cursiv::ReferenceInfo site{};
  site.referenceText = "Same";
  const auto candidates = world.ws->linker->getCandidates(site);
  CHECK(candidates.has_value());
  if (!candidates) return;
  CHECK(candidates->size() == 1024);
  for (std::size_t i = 0; i < candidates->size(); ++i)
    CHECK((*candidates)[i] == &rows->entries[i]);
}

// The sentence names the type the member wanted, which is the half that tells
// a reader why a visible name was refused: `Entity::superType` holds entities,
// and `String` is a datatype.
void the_failure_names_the_expected_type() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person extends String { }\n");
  const cursiv::ReferenceInfo site =
      site_of(ws::generated::Workspace::kAstMetaData,
              document->parseResult.references->handles.front(), "String");

  const cursiv::AstNodeDescriptionOrError refused =
      world.ws->linker->getCandidate(site);
  CHECK(!refused.has_value());
  CHECK(refused.error().message ==
        "Could not resolve reference to Entity named 'String'.");
  // `targetDescription` is for a name that did reach a description -- never
  // here, where nothing answered at all.
  CHECK(refused.error().targetDescription == nullptr);
}

// A name nothing declares reads the same way, and that is deliberate: the
// member's type is what the reader has to satisfy either way.
void a_name_nothing_declares_reads_the_same() {
  const World world;
  const auto document = world.scoped("entity Person extends Absent { }\n");
  const cursiv::ReferenceInfo site =
      site_of(ws::generated::Workspace::kAstMetaData,
              document->parseResult.references->handles.front(), "Absent");

  const cursiv::AstNodeDescriptionOrError refused =
      world.ws->linker->getCandidate(site);
  CHECK(!refused.has_value());
  CHECK(refused.error().message ==
        "Could not resolve reference to Entity named 'Absent'.");
}

struct CaseInsensitiveProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;

 protected:
  [[nodiscard]] bool isCaseSensitive(
      const cursiv::ReferenceInfo&) const noexcept override {
    return false;
  }
};

// `StrinG` resolves `String` under an insensitive provider, and the linker
// itself compares nothing: `getCandidate` and `getCandidates` answer the same
// walk, under both policies.
void the_policy_decides_what_a_reference_reaches() {
  World world;
  const auto document =
      world.scoped("datatype String\nentity HasAuthor { author : StrinG }\n");

  // Case-sensitive (the default): no target.
  world.ws->linker->link(*document);
  CHECK(World::feature(*document).type.get() == nullptr);

  // The same document under an insensitive provider: the reference reaches
  // the `String` the author declared, spelling untouched.
  world.ws->scopeProvider =
      std::make_unique<CaseInsensitiveProvider>(*world.ws);
  world.ws->linker->unlink(*document);
  world.ws->linker->link(*document);
  const ws::ast::Type* const target = World::feature(*document).type.get();
  CHECK(target != nullptr);
  CHECK(world.ws->nameProvider->getName(*target) ==
        std::optional<std::string_view>("String"));

  // The two candidate questions agree, head and set.
  const cursiv::ReferenceInfo site =
      site_of(ws::generated::Workspace::kAstMetaData,
              document->parseResult.references->handles.front(), "StrinG");
  const cursiv::AstNodeDescriptionOrError one =
      world.ws->linker->getCandidate(site);
  const cursiv::AstNodeDescriptionsOrError all =
      world.ws->linker->getCandidates(site);
  CHECK(one.has_value() && all.has_value());
  CHECK(!all->empty() && all->front() == *one);
  CHECK((*one)->name == "String");
}

// An alias-chasing provider: its answer depends on the candidate's own
// reference, read as any reader reads it.
struct ChasingProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;

  [[nodiscard]] const cursiv::AstNodeDescription* getScopeEntry(
      const cursiv::ReferenceInfo& context,
      std::string_view name) const override {
    const cursiv::AstNodeDescription* const found =
        cursiv::DefaultScopeProvider::getScopeEntry(context, name);
    if (found == nullptr || found->node == nullptr) return found;
    if (const auto* const entity =
            cursiv::ast_cast<const ws::ast::Entity>(found->node))
      if (entity->superType.get() == nullptr)
        return nullptr;  // the chase failed, so the answer is unknown
    return found;
  }
};

// A provider reading through its own resolution says cycle, not "not found".
void a_cycle_is_told_apart_from_a_missing_name() {
  World world;
  const auto document = world.scoped("entity A extends A { }\n");
  world.ws->scopeProvider = std::make_unique<ChasingProvider>(*world.ws);
  world.ws->linker->link(*document);

  const auto& a = *static_cast<const ws::ast::Entity*>(
      document->parseResult.references->handles.front().owner);
  CHECK(a.superType.state() == cursiv::RefState::ErrorCycle);
}

// Every link of the loop says cycle, not just the one that noticed: a loop of
// two failed because of the loop, on both sides.
void every_link_of_a_loop_says_cycle() {
  World world;
  const auto document =
      world.scoped("entity A extends B { }\nentity B extends A { }\n");
  world.ws->scopeProvider = std::make_unique<ChasingProvider>(*world.ws);
  world.ws->linker->link(*document);

  const auto& refs = *document->parseResult.references;
  CHECK(refs.handles.size() == 2);
  const auto& a =
      *static_cast<const ws::ast::Entity*>(refs.handles.front().owner);
  const auto& b =
      *static_cast<const ws::ast::Entity*>(refs.handles.back().owner);
  CHECK(a.superType.state() == cursiv::RefState::ErrorCycle);
  CHECK(b.superType.state() == cursiv::RefState::ErrorCycle);
}

struct ThrowingProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;

  [[nodiscard]] const cursiv::AstNodeDescription* getScopeEntry(
      const cursiv::ReferenceInfo& context,
      std::string_view name) const override {
    if (name == "Boom") throw std::runtime_error("provider exploded");
    return cursiv::DefaultScopeProvider::getScopeEntry(context, name);
  }
};

// A provider that throws fails the reference it was resolving; the rest of
// the work list still links.
void a_throwing_provider_fails_only_its_reference() {
  World world;
  const auto document = world.scoped(
      "datatype String\nentity P { a : Boom }\nentity Q { b : String }\n");
  world.ws->scopeProvider = std::make_unique<ThrowingProvider>(*world.ws);
  world.ws->linker->link(*document);

  const auto& refs = *document->parseResult.references;
  CHECK(refs.handles.size() == 2);
  const auto& boomed =
      *static_cast<const ws::ast::Feature*>(refs.handles.front().owner);
  const auto& sound =
      *static_cast<const ws::ast::Feature*>(refs.handles.back().owner);
  CHECK(boomed.type.state() == cursiv::RefState::ErrorNotFound);
  CHECK(sound.type.get() != nullptr);
}

// Throws on `Boom`, and reads each found entity's own reference without
// needing it to resolve.
struct PeekingProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;

  [[nodiscard]] const cursiv::AstNodeDescription* getScopeEntry(
      const cursiv::ReferenceInfo& context,
      std::string_view name) const override {
    if (name == "Boom") throw std::runtime_error("provider exploded");
    const cursiv::AstNodeDescription* const found =
        cursiv::DefaultScopeProvider::getScopeEntry(context, name);
    if (found != nullptr && found->node != nullptr)
      if (const auto* const entity =
              cursiv::ast_cast<const ws::ast::Entity>(found->node))
        (void)entity->superType.get();
    return found;
  }
};

// The exception stays on the reference whose provider threw: a read of it
// answers the failure, and a provider that read it carries on.
void a_throwing_provider_stays_on_its_reference() {
  World world;
  const auto document = world.scoped(
      "entity A extends B { }\nentity B extends Boom { }\n");
  world.ws->scopeProvider = std::make_unique<PeekingProvider>(*world.ws);
  document->parseResult.references->linker = world.ws->linker.get();
  document->parseResult.references->document = document.get();
  const auto& refs = *document->parseResult.references;
  const auto& a =
      *static_cast<const ws::ast::Entity*>(refs.handles.front().owner);
  const auto& b =
      *static_cast<const ws::ast::Entity*>(refs.handles.back().owner);

  bool threw = false;
  try {
    CHECK(a.superType.get() == &b);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(!threw);
  CHECK(b.superType.state() == cursiv::RefState::ErrorNotFound);
}

// Moves the site's document past `ComputedScopes` while it looks, as the
// worker scoping that document does beside a read, and finds nothing.
struct RungMovingProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;
  cursiv::Document* moved = nullptr;

  [[nodiscard]] const cursiv::AstNodeDescription* getScopeEntry(
      const cursiv::ReferenceInfo&, std::string_view) const override {
    moved->state = cursiv::DocumentState::ComputedScopes;
    return nullptr;
  }
};

// Judged on the rung the read began at: a failure that started below the
// scopes is not kept, whatever rung the document reached meanwhile.
void an_early_failure_is_judged_where_the_read_began() {
  World world;
  const auto document = world.shared.documentFactory->fromString(
      "datatype String\nentity Person { age : String }\n", "file:///w/a.ws");
  world.shared.documents->addDocument(document);
  document->state = cursiv::DocumentState::IndexedContent;
  auto provider = std::make_unique<RungMovingProvider>(*world.ws);
  provider->moved = document.get();
  world.ws->scopeProvider = std::move(provider);
  document->parseResult.references->linker = world.ws->linker.get();
  document->parseResult.references->document = document.get();

  CHECK(World::feature(*document).type.state() ==
        cursiv::RefState::Unresolved);
}

// Echoes every entry twice, as an outer scope re-surfacing what an inner one
// already gave does.
struct EchoingProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;
  using DefaultScopeProvider::visitScopeEntries;

  [[nodiscard]] bool visitScopeEntries(
      const cursiv::ReferenceInfo& context, std::string_view name,
      cursiv::function_ref<bool(const cursiv::AstNodeDescription&)> visitor)
      const override {
    return cursiv::DefaultScopeProvider::visitScopeEntries(
        context, name, [&](const cursiv::AstNodeDescription& entry) {
          return visitor(entry) && visitor(entry);
        });
  }
};

void a_candidate_is_answered_once() {
  World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  world.ws->scopeProvider = std::make_unique<EchoingProvider>(*world.ws);

  const cursiv::ReferenceInfo site =
      site_of(ws::generated::Workspace::kAstMetaData,
              document->parseResult.references->handles.front(), "String");
  const cursiv::AstNodeDescriptionsOrError all =
      world.ws->linker->getCandidates(site);
  CHECK(all.has_value());
  CHECK(all->size() == 1);
}

struct CapturingSink final : cursiv::ObservabilitySink {
  std::vector<cursiv::Observation> rows;
  void publish(const cursiv::Observation& o) noexcept override {
    rows.push_back(o);
  }
};

// A lookup before the local symbols exist fails, and the sink says so.
void an_early_read_is_said() {
  World world;
  auto sink = std::make_unique<CapturingSink>();
  CapturingSink& sunk = *sink;
  world.shared.observabilitySink = std::move(sink);

  const auto document = world.shared.documentFactory->fromString(
      "datatype String\nentity Person { age : String }\n",
      "file:///w/a.ws");
  world.shared.documents->addDocument(document);

  const cursiv::ReferenceInfo site =
      site_of(ws::generated::Workspace::kAstMetaData,
              document->parseResult.references->handles.front(), "String");
  (void)world.ws->linker->getCandidate(site);
  CHECK(!sunk.rows.empty());
  if (!sunk.rows.empty())
    CHECK(sunk.rows.front().code ==
          cursiv::ObservationCode::ScopeReadBeforeComputed);
}

// (subtype, supertype), the reference's own order: an Entity is a Type, a
// Type is not an Entity.
void is_subtype_reads_subtype_first() {
  const World world;
  const cursiv::AstReflection& r = *world.ws->astReflection;
  const cursiv::AstTypeTag* entity = &cursiv::ast_type_tag<ws::ast::Entity>;
  const cursiv::AstTypeTag* type = &cursiv::ast_type_tag<ws::ast::Type>;
  CHECK(r.isSubtype(entity, type));
  CHECK(!r.isSubtype(type, entity));
  CHECK(r.isSubtype(entity, nullptr));  // null supertype accepts everything
  CHECK(!r.isSubtype(nullptr, type));
}

// Cancellation is the one exception `forceAll` does not isolate: it aborts
// the list, where a provider's own failure loses one reference.
struct CancellingProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;
  cursiv::CancellationTokenSource* source = nullptr;

  [[nodiscard]] const cursiv::AstNodeDescription* getScopeEntry(
      const cursiv::ReferenceInfo& context,
      std::string_view name) const override {
    if (source != nullptr) source->request_stop();
    cursiv::throw_if_cancelled(source->get_token());
    return cursiv::DefaultScopeProvider::getScopeEntry(context, name);
  }
};

void cancellation_aborts_the_whole_link() {
  World world;
  const auto document = world.scoped(
      "datatype String\nentity P { a : String }\nentity Q { b : String }\n");
  auto provider = std::make_unique<CancellingProvider>(*world.ws);
  cursiv::CancellationTokenSource source;
  provider->source = &source;
  world.ws->scopeProvider = std::move(provider);

  bool cancelled = false;
  try {
    world.ws->linker->link(*document, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
}

// Failed means an error state: before a link everything is unresolved and
// nothing has failed yet.
void any_unresolved_means_failed_not_pending() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity P { a : Absent }\n");
  CHECK(!document->parseResult.references->anyUnresolved());
  world.ws->linker->link(*document);
  CHECK(document->parseResult.references->anyUnresolved());
  world.ws->linker->unlink(*document);
  CHECK(!document->parseResult.references->anyUnresolved());
}

namespace ns {

using tns::Kind1;
using tns::Kind2;
using tns::Use;

struct NsWorld {
  cursiv::SharedCoreServices shared;
  cursiv::CoreServices* lang = nullptr;

  NsWorld() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_core_services(shared);
    auto language = std::make_unique<cursiv::CoreServices>(shared);
    language->languageMetaData = {"narrowedsite", {".ns"}, {}};
    language->parser = tns::generated::NarrowedSite::create_parser();
    language->astReflection =
        tns::generated::NarrowedSite::create_ast_reflection();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    cursiv::install_default_core_services(*language);
    lang = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }

  std::shared_ptr<cursiv::Document> scoped(std::string text) const {
    auto document =
        shared.documentFactory->fromString(std::move(text), "file:///w/a.ns");
    shared.documents->addDocument(document);
    document->localSymbols =
        lang->scopeComputation->collectLocalSymbols(*document);
    document->state = cursiv::DocumentState::ComputedScopes;
    return document;
  }

  // The `use` site: the first reference the work list names.
  static Use& use(const cursiv::Document& document) {
    return *static_cast<Use*>(
        document.parseResult.references->handles.front().owner);
  }
};

// The member admits both kinds and the site narrows to `Kind1`: the name is
// looked up among the `Kind1` only, so a nearer `Kind2` of that name is no
// candidate.
void a_homonym_of_another_class_is_skipped() {
  const NsWorld world;
  const auto document = world.scoped("two b one b use b");
  world.lang->linker->link(*document);

  Use& site = NsWorld::use(*document);
  CHECK(site.what.state() == cursiv::RefState::Resolved);
  CHECK(cursiv::ast_cast<Kind1>(site.what.get()) != nullptr);
}

// Nothing of the site's class carries the name: not found, with no fallback
// target.
void a_name_only_another_class_holds_is_not_found() {
  const NsWorld world;
  const auto document = world.scoped("one a two b use b");
  world.lang->linker->link(*document);

  Use& site = NsWorld::use(*document);
  CHECK(site.what.state() == cursiv::RefState::ErrorNotFound);
  CHECK(site.what.get() == nullptr);
  CHECK(site.what.target == nullptr);
  CHECK(document->parseResult.references->anyUnresolved());
}

// The same site with a fitting name: the narrowing resolves as it always did.
void a_narrow_that_fits_still_resolves() {
  const NsWorld world;
  const auto document = world.scoped("one a two b use a");
  world.lang->linker->link(*document);

  Use& site = NsWorld::use(*document);
  CHECK(site.what.state() == cursiv::RefState::Resolved);
  CHECK(site.what.get() != nullptr);
  CHECK(cursiv::ast_cast<Kind1>(site.what.target) != nullptr);
}

// A failed narrowing draws no edge: navigation follows what resolved.
void a_failed_narrowing_draws_no_edge() {
  const NsWorld world;
  const auto document = world.scoped("one a two b use b");
  world.lang->linker->link(*document);

  bool seen = false;
  document->parseResult.references->visitResolved(
      document->parseResult.cst(),
      [&](cursiv::TextOffset, cursiv::TextOffset, const cursiv::AstNode&,
          bool) { seen = true; });
  CHECK(!seen);
}

// The plural door looks up its class only too.
void a_multi_site_looks_up_its_class_only() {
  const NsWorld world;
  const auto fits = world.scoped("two b one b use b all b");
  world.lang->linker->link(*fits);
  Use& skipped = NsWorld::use(*fits);
  CHECK(skipped.all.state() == cursiv::RefState::Resolved);
  CHECK(skipped.all.items.size() == 1);
  CHECK(cursiv::ast_cast<Kind1>(skipped.all.items.front().target) != nullptr);

  const NsWorld other;
  const auto missing = other.scoped("one a two b use a all b");
  other.lang->linker->link(*missing);
  Use& site = NsWorld::use(*missing);
  CHECK(site.what.state() == cursiv::RefState::Resolved);
  CHECK(site.all.state() == cursiv::RefState::ErrorNotFound);
  CHECK(site.all.items.empty());
}

// The sentence names the site's class, and one protected method rewords it.
struct Reworded final : cursiv::DefaultDocumentValidator {
  using DefaultDocumentValidator::DefaultDocumentValidator;

 protected:
  [[nodiscard]] std::string linkingErrorMessage(
      cursiv::RefState why, std::uint16_t reference, std::uint8_t narrow,
      std::string_view text) const override {
    if (why == cursiv::RefState::ErrorNotFound)
      return "not that kind: '" + std::string(text) + "'";
    return cursiv::DefaultDocumentValidator::linkingErrorMessage(why, reference,
                                                                 narrow, text);
  }
};

void the_diagnostic_names_the_site_s_class() {
  NsWorld world;
  const auto document = world.scoped("one a two b use b");
  world.lang->linker->link(*document);

  const auto has = [](const std::vector<cursiv::Diagnostic>& diagnostics,
                      std::string_view message) {
    for (const cursiv::Diagnostic& d : diagnostics)
      if (d.message == message) return true;
    return false;
  };

  std::vector<cursiv::Diagnostic> diagnostics;
  world.lang->documentValidator->validateDocument(
      *document, diagnostics, cursiv::ValidationOptions{}, {});
  CHECK(has(diagnostics, "Expecting Kind1 named 'b'."));

  world.lang->documentValidator = std::make_unique<Reworded>(*world.lang);
  diagnostics.clear();
  world.lang->documentValidator->validateDocument(
      *document, diagnostics, cursiv::ValidationOptions{}, {});
  CHECK(has(diagnostics, "not that kind: 'b'"));
}

}  // namespace ns

}  // namespace

void run() {
  linking_results_distinguish_null_from_an_error();
  a_name_points_at_what_it_declares();
  reading_a_reference_resolves_it();
  an_unknown_name_resolves_to_nothing();
  unlink_forgets_every_answer();
  the_candidate_is_the_scope_s_first_fit();
  plural_candidates_keep_first_identity_in_scope_order();
  the_failure_names_the_expected_type();
  a_name_nothing_declares_reads_the_same();
  the_policy_decides_what_a_reference_reaches();
  a_cycle_is_told_apart_from_a_missing_name();
  every_link_of_a_loop_says_cycle();
  a_throwing_provider_fails_only_its_reference();
  a_throwing_provider_stays_on_its_reference();
  an_early_failure_is_judged_where_the_read_began();
  a_candidate_is_answered_once();
  an_early_read_is_said();
  an_early_read_is_read_again();
  is_subtype_reads_subtype_first();
  ns::a_homonym_of_another_class_is_skipped();
  ns::a_name_only_another_class_holds_is_not_found();
  ns::a_narrow_that_fits_still_resolves();
  ns::a_failed_narrowing_draws_no_edge();
  ns::a_multi_site_looks_up_its_class_only();
  ns::the_diagnostic_names_the_site_s_class();
  any_unresolved_means_failed_not_pending();
  cancellation_aborts_the_whole_link();
}

}  // namespace linker

// Pins the declared name of a node and where it was written. The dispatch is
// typed because reading a member needs the member's type; the interface it
// answers through is not.
namespace name_provider {

namespace {

// A container, because `getNameNode` asks the grammar which record wrote the
// name. `getName` still asks it nothing -- that is what keeps this service off
// a template.
cursiv::CoreServices& language() {
  static cursiv::SharedCoreServices shared;
  static cursiv::CoreServices services(shared);
  static const bool ready = [] {
    services.languageMetaData = {"workspace", {".ws"}, {}};
    services.parser = ws::generated::Workspace::create_parser();
    return true;
  }();
  (void)ready;
  return services;
}

const cursiv::NameProvider& provider() {
  static const cursiv::DefaultNameProvider instance(language());
  return instance;
}

// A real document, because that is what a caller has: the tree the node's id
// indexes lives in it, and the AST borrows every identifier it holds.
struct Parsed {
  cursiv::Document document;
  cursiv::ParseResult& result;

  explicit Parsed(std::string text)
      : document(cursiv::TextDocument::create("file:///t.ws", "ws", 1,
                                              std::move(text))),
        result(document.parseResult) {
    result = language().parser->parse(document.textDocument().getText());
    if (result.value != nullptr) result.value->parent = &document;
  }
};

// Walks the AST for the first node whose name matches.
const cursiv::AstNode* find_named(const cursiv::ParseResult& r,
                                  std::string_view name) {
  for (const cursiv::AstNode* const node : r.nodes) {
    const auto found = provider().getName(*node);
    if (found.has_value() && *found == name) return node;
  }
  return nullptr;
}

void a_named_node_answers_its_name() {
  const Parsed p("datatype String\nentity Person { age : String }\n");
  CHECK(p.result.value != nullptr);

  const cursiv::AstNode* const person = find_named(p.result, "Person");
  CHECK(person != nullptr);
  CHECK(find_named(p.result, "String") != nullptr);
  CHECK(find_named(p.result, "age") != nullptr);
  // Nothing declares this.
  CHECK(find_named(p.result, "nope") == nullptr);
}

// The root of a workspace is unnamed: it declares no `name` member.
void an_unnamed_node_answers_nothing() {
  const Parsed p("datatype String\n");
  CHECK(!provider().getName(*p.result.value).has_value());
}

// The range is where the text is, the name is what it means.
void the_name_node_is_the_text_it_was_read_from() {
  const Parsed p("datatype String\nentity Person { age : String }\n");
  const std::string_view src = p.document.textDocument().getText();
  const cursiv::AstNode* const person = find_named(p.result, "Person");

  // The document is found by the climb; passing it explicitly is the same
  // answer, one lookup cheaper.
  const auto nameNode = provider().getNameNode(*person);
  const auto passed = provider().getNameNode(*person, &p.document);
  CHECK(nameNode.has_value());
  CHECK(passed.has_value() && passed->id == nameNode->id);
  CHECK(static_cast<bool>(*nameNode));
  CHECK(nameNode->text() == "Person");
  CHECK(nameNode->begin() == src.find("Person"));
  CHECK(nameNode->end() == src.find("Person") + 6);
}

void an_unnamed_node_has_no_name_node() {
  const Parsed p("datatype String\n");
  CHECK(!provider().getNameNode(*p.result.value).has_value());
}

// A node built by hand belongs to no tree: it has a name and no range.
void a_handmade_node_has_a_name_and_no_range() {
  const Parsed p("datatype String\n");
  ws::ast::DataType made;
  made.type = &cursiv::ast_type_tag<ws::ast::DataType>;
  made.name = "Invented";
  CHECK(provider().getName(made) ==
        std::optional<std::string_view>("Invented"));
  CHECK(made.cstNodeId == cursiv::kNoNode);
  CHECK(!provider().getNameNode(made).has_value());
}

// A declared name that matched nothing is the empty name, not "no name": the
// class answers, and what it spells is the caller's to judge.
void an_empty_name_is_still_a_name() {
  ws::ast::DataType made;
  made.type = &cursiv::ast_type_tag<ws::ast::DataType>;
  const std::optional<std::string_view> name = provider().getName(made);
  CHECK(name.has_value());
  CHECK(name->empty());
}

}  // namespace

void run() {
  a_named_node_answers_its_name();
  an_unnamed_node_answers_nothing();
  the_name_node_is_the_text_it_was_read_from();
  an_unnamed_node_has_no_name_node();
  a_handmade_node_has_a_name_and_no_range();
  an_empty_name_is_still_a_name();
}

}  // namespace name_provider

// Pins navigation over a multi reference: a declaration one targets stands
// for its whole overload set -- in the answer under the caret, and in the
// rows find-references emits.
namespace references {

namespace {

namespace ct = cursiv::test;

// The LSP storey too: what the editor is answered about a group is the same
// fact, one floor up.
struct World {
  struct Shared : cursiv::SharedLspServices {};
  struct Language : cursiv::LspServices {
    explicit Language(Shared& s)
        : cursiv::CoreServices(s), cursiv::LspServices(s) {}
  };

  Shared shared;
  Language* mref = nullptr;

  World() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_lsp_services(shared);
    auto language = std::make_unique<Language>(shared);
    language->languageMetaData = {"multiref", {".mref"}, {}};
    language->parser = fmulti::generated::MultiG::create_parser();
    language->astReflection =
        fmulti::generated::MultiG::create_ast_reflection();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    cursiv::install_default_core_services(*language);
    cursiv::install_default_lsp_services(*language);
    mref = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }

  cursiv::Document* built(std::string text) const {
    auto document =
        shared.documentFactory->fromString(std::move(text), "file:///q.mref");
    cursiv::Document* const raw = document.get();
    shared.documents->addDocument(std::move(document));
    cursiv::Document* const set[] = {raw};
    shared.documentBuilder->build(set);
    return raw;
  }
};

void navigation_uses_the_documents_language_id() {
  World world;
  auto document = world.shared.documentFactory->fromTextDocument(
      cursiv::TextDocument::create("file:///without-extension", "multiref", 1,
                                   "SELECT t FROM t, t"));
  world.shared.documents->addDocument(document);
  cursiv::Document* const set[] = {document.get()};
  world.shared.documentBuilder->build(set);
  const auto declaration = world.mref->references->findDeclaration(*document, 14);
  CHECK(declaration.targets.size() == 2);
  if (declaration.targets.size() != 2) return;
  const auto references = world.mref->references->findReferences(
      *declaration.targets.front(), {.includeDeclaration = true});
  CHECK(references.size() == 3);
  CHECK(references[1].sourceBegin == 14 && references[1].sourceEnd == 15);
  CHECK(references[2].sourceBegin == 17 && references[2].sourceEnd == 18);
}

void navigation_resolves_a_reference_at_computed_scopes() {
  linker::World world;
  cursiv::CancellationTokenSource lookupCancellation;
  struct CancellingScope final : cursiv::DefaultScopeProvider {
    using DefaultScopeProvider::DefaultScopeProvider;
    cursiv::CancellationTokenSource* source = nullptr;
    const cursiv::AstNodeDescription* getScopeEntry(
        const cursiv::ReferenceInfo& context,
        std::string_view name) const override {
      const auto* found = DefaultScopeProvider::getScopeEntry(context, name);
      if (source != nullptr) source->request_stop();
      return found;
    }
  };
  auto scope = std::make_unique<CancellingScope>(*world.ws);
  auto* customScope = scope.get();
  customScope->source = &lookupCancellation;
  world.ws->scopeProvider = std::move(scope);
  const std::string text = "datatype T entity E { f : T g : T }";
  auto document =
      world.shared.documents->createDocument("file:///lazy.ws", text);
  cursiv::Document* documents[] = {document.get()};
  cursiv::CancellationTokenSource buildCancellation;
  struct Cut final : cursiv::DocumentPhaseListener {
    cursiv::CancellationTokenSource* source;
    void onDocumentPhase(cursiv::Document&, cursiv::DocumentState state,
                         const cursiv::CancellationToken&) override {
      if (state == cursiv::DocumentState::ComputedScopes)
        source->request_stop();
    }
  } cut;
  cut.source = &buildCancellation;
  auto& builder = *world.shared.documentBuilder;
  auto& lock = *world.shared.workspaceLock;
  builder.addDocumentPhaseListener(cut);
  lock.write([&](const cursiv::CancellationToken&) {
    builder.build(documents, {}, buildCancellation.get_token());
  });
  builder.removeDocumentPhaseListener(cut);
  CHECK(document->state.load() == cursiv::DocumentState::ComputedScopes);
  const auto offset = static_cast<cursiv::TextOffset>(text.find("T g"));
  bool cancelled = false;
  (void)lock.read([&] {
    try {
      (void)world.ws->references->findDeclaration(
          *document, offset, lookupCancellation.get_token());
    } catch (const cursiv::OperationCancelled&) {
      cancelled = true;
    }
  });
  CHECK(cancelled);
  customScope->source = nullptr;
  (void)lock.read([&] {
    const auto found = world.ws->references->findDeclaration(*document, offset);
    CHECK(found.targets.size() == 1);
    CHECK(found.span.begin == offset && found.span.end == offset + 1);
  });
}

// The grammar knows at compile time that it holds one.
void the_grammar_confesses_its_multi_reference() {
  CHECK(
      fmulti::generated::MultiG::create_ast_reflection()->hasMultiReference());
}

// `SELECT t FROM t, t`: one written column name, both table items at once.
void a_declaration_stands_for_its_group(bool declaresMulti) {
  auto metadata = fmulti::generated::MultiG::kAstMetaData;
  metadata.anyMultiReference = declaresMulti;
  const World world;
  // Declaration metadata cannot rule out a group formed by incoming references.
  world.mref->astReflection =
      std::make_unique<cursiv::DefaultAstReflection>(metadata);
  cursiv::Document* const document = world.built("SELECT t FROM t, t");
  //                                          0123456789012345678

  // Caret on the first table item's name: the answer is the whole group,
  // whichever member the caret sat on.
  const cursiv::Declaration found =
      world.mref->references->findDeclaration(*document, 14);
  CHECK(found.targets.size() == 2);
  if (found.targets.size() != 2) return;

  // find-references from one member: the group's two declarations and the
  // one written site, in reading order.
  const std::vector<cursiv::ReferenceDescription> rows =
      world.mref->references->findReferences(*found.targets.front(),
                                             {.includeDeclaration = true});
  CHECK(rows.size() == 3);
  if (rows.size() != 3) return;
  CHECK(rows[0].sourceBegin == 7 && rows[0].multiReference);
  CHECK(rows[1].sourceBegin == 14 && rows[1].local);
  CHECK(rows[2].sourceBegin == 17 && rows[2].local);

  // ...and the editor is answered those same three sites, from that caret.
  (void)ct::expectFindReferences(*world.mref,
                                 {.text = "SELECT <|t|> FROM <|><|t|>, <|t|>",
                                  .includeDeclaration = true,
                                  .disposeAfterCheck = true});
}

// Without the declarations, only the written site remains -- the group adds
// rows to `includeDeclaration`, never to the plain question.
void the_group_does_not_leak_into_plain_references() {
  const World world;
  // The caret sits on a table item, and the answer is the written name alone.
  (void)ct::expectFindReferences(*world.mref,
                                 {.text = "SELECT <|t|> FROM <|>t, t",
                                  .includeDeclaration = false,
                                  .disposeAfterCheck = true});
}

// Two documents submitted together take the builder's real pool path
// (`DefaultDocumentBuilder`'s `parallelFor`), not the single-document case
// every lane of this group otherwise takes: each keeps its own group.
void two_documents_built_together_keep_their_own_group() {
  const World world;
  auto docA = world.shared.documentFactory->fromString("SELECT t FROM t, t",
                                                       "file:///a.mref");
  auto docB = world.shared.documentFactory->fromString("SELECT u FROM u, u, u",
                                                       "file:///b.mref");
  cursiv::Document* const a = docA.get();
  cursiv::Document* const b = docB.get();
  world.shared.documents->addDocument(std::move(docA));
  world.shared.documents->addDocument(std::move(docB));
  cursiv::Document* const set[] = {a, b};
  world.shared.documentBuilder->build(set);

  const cursiv::Declaration foundA =
      world.mref->references->findDeclaration(*a, 14);
  CHECK(foundA.targets.size() == 2);
  const cursiv::Declaration foundB =
      world.mref->references->findDeclaration(*b, 14);
  CHECK(foundB.targets.size() == 3);
}

void a_multi_reference_tracks_new_global_candidates(bool ignoreCase) {
  World world;
  struct ExportItems final : cursiv::DefaultScopeComputation {
    using DefaultScopeComputation::DefaultScopeComputation;

    std::vector<cursiv::AstNodeDescription> collectExportedSymbols(
        const cursiv::Document& document,
        const cursiv::CancellationToken& token) const override {
      std::vector<cursiv::AstNodeDescription> exported;
      const auto* model = document.parseResult.value_as<fmulti::ast::Model>();
      if (model == nullptr || model->query == nullptr) return exported;
      for (auto* item : model->query->items) {
        cursiv::throw_if_cancelled(token);
        addExportedSymbol(*item, exported, document);
      }
      return exported;
    }
  };
  struct IgnoreCase final : cursiv::DefaultScopeProvider {
    using DefaultScopeProvider::DefaultScopeProvider;

   protected:
    bool isCaseSensitive(const cursiv::ReferenceInfo&) const noexcept override {
      return false;
    }
  };
  world.mref->scopeComputation = std::make_unique<ExportItems>(*world.mref);
  if (ignoreCase)
    world.mref->scopeProvider = std::make_unique<IgnoreCase>(*world.mref);

  constexpr std::string_view kA = "file:///a.mref";
  constexpr std::string_view kB = "file:///b.mref";
  constexpr std::string_view kC = "file:///c.mref";
  constexpr std::string_view kZ = "file:///z.mref";
  const auto buildExport = [&](std::string_view uri, std::string_view name) {
    std::shared_ptr<cursiv::Document> document;
    world.shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
      document = world.shared.documents->createDocument(
          uri, "SELECT " + std::string(name) + " FROM " + std::string(name), token);
      cursiv::Document* const changed[] = {document.get()};
      world.shared.documentBuilder->build(changed, {}, token);
    });
    return document;
  };

  (void)buildExport(kB, "Shared");
  std::shared_ptr<cursiv::Document> consumer;
  world.shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
    consumer = world.shared.documents->createDocument(
        kC, ignoreCase ? "SELECT sHaReD FROM Local" : "SELECT Shared FROM Local",
        token);
    cursiv::Document* const changed[] = {consumer.get()};
    world.shared.documentBuilder->build(changed, {}, token);
  });
  const auto* root = consumer->parseResult.value;
  const auto revision = consumer->buildRevision;
  const auto expectTargets =
      [&](std::initializer_list<std::string_view> expected) {
    CHECK(consumer->parseResult.value == root);
    CHECK(consumer->buildRevision == revision);
    CHECK(consumer->state.load() == cursiv::DocumentState::Validated);
    CHECK(!consumer->hasLinkingErrors());
    const auto* model = consumer->parseResult.value_as<fmulti::ast::Model>();
    CHECK(model != nullptr && model->query != nullptr &&
          model->query->pick != nullptr);
    if (model == nullptr || model->query == nullptr || model->query->pick == nullptr)
      return;
    const auto& reference = model->query->pick->name;
    std::vector<std::string_view> actual;
    for (std::size_t i = 0; i < reference.size(); ++i) {
      const auto* target = reference.get(i);
      CHECK(target != nullptr && target->getDocument() != nullptr);
      if (target != nullptr && target->getDocument() != nullptr)
        actual.push_back(target->getDocument()->uri);
    }
    CHECK(actual == std::vector<std::string_view>(expected));
  };
  expectTargets({kB});

  const auto a = buildExport(kA, ignoreCase ? "SHARED" : "Shared");
  expectTargets({kA, kB});
  (void)buildExport(kZ, ignoreCase ? "shared" : "Shared");
  expectTargets({kA, kB, kZ});

  world.shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
    const std::string_view deleted[] = {kA};
    world.shared.documentBuilder->update({}, deleted, token);
  });
  CHECK(!world.shared.documents->hasDocument(kA));
  expectTargets({kB, kZ});

  const auto recreated = buildExport(kA, ignoreCase ? "SHARED" : "Shared");
  CHECK(recreated->id == a->id);
  expectTargets({kA, kB, kZ});
}

// Throws on `Boom`; asked for `good`, first reads another document's multi
// reference, as a provider consulting another site may.
struct PeekingMultiProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;
  using DefaultScopeProvider::visitScopeEntries;
  const fmulti::ast::Pick* other = nullptr;

  [[nodiscard]] bool visitScopeEntries(
      const cursiv::ReferenceInfo& context, std::string_view name,
      cursiv::function_ref<bool(const cursiv::AstNodeDescription&)> visitor)
      const override {
    if (name == "Boom") throw std::runtime_error("provider exploded");
    if (name == "good" && other != nullptr) (void)other->name.size();
    return cursiv::DefaultScopeProvider::visitScopeEntries(context, name,
                                                           visitor);
  }
};

// The exception stays on the multi reference whose provider threw, whichever
// document links first: the site that read it still resolves, and only the
// failed one is reported.
void a_throwing_provider_stays_on_its_multi_reference(bool failedFirst) {
  World world;
  // Inline, so the documents link in the order they are given.
  world.shared.taskScheduler = std::make_unique<cursiv::TaskScheduler>(0);
  auto provider = std::make_unique<PeekingMultiProvider>(*world.mref);
  PeekingMultiProvider* const peeking = provider.get();
  world.mref->scopeProvider = std::move(provider);
  const auto add = [&](std::string text, std::string uri) {
    auto document = world.shared.documentFactory->fromString(std::move(text),
                                                             std::move(uri));
    cursiv::Document* const raw = document.get();
    world.shared.documents->addDocument(std::move(document));
    return raw;
  };
  cursiv::Document* const good =
      add("SELECT good FROM good", "file:///good.mref");
  cursiv::Document* const bad =
      add("SELECT Boom FROM good", "file:///bad.mref");
  const auto pick = [](const cursiv::Document& document) -> fmulti::ast::Pick& {
    return *document.parseResult.value_as<fmulti::ast::Model>()->query->pick;
  };
  peeking->other = &pick(*bad);

  std::vector<cursiv::Document*> order{good, bad};
  if (failedFirst) std::ranges::reverse(order);
  world.shared.workspaceLock->write(
      [&](const cursiv::CancellationToken& token) {
        world.shared.documentBuilder->build(order, {}, token);
      });

  CHECK(good->state.load() == cursiv::DocumentState::Validated);
  CHECK(pick(*good).name.state() == cursiv::RefState::Resolved);
  CHECK(pick(*good).name.size() == 1);
  CHECK(good->diagnostics.empty());
  CHECK(pick(*bad).name.state() == cursiv::RefState::ErrorNotFound);
  CHECK(bad->diagnostics.size() == 1);
}

// Exports both nodes of the pair: a child is no top-level symbol until a
// language widens its exports, as this one does.
struct ExportPair final : cursiv::DefaultScopeComputation {
  using DefaultScopeComputation::DefaultScopeComputation;

  std::vector<cursiv::AstNodeDescription> collectExportedSymbols(
      const cursiv::Document& document,
      const cursiv::CancellationToken&) const override {
    std::vector<cursiv::AstNodeDescription> exported;
    auto* const model = document.parseResult.value_as<tsr::Model>();
    if (model == nullptr || model->pair == nullptr) return exported;
    addExportedSymbol(*model->pair, exported, document);
    if (model->pair->child != nullptr)
      addExportedSymbol(*model->pair->child, exported, document);
    return exported;
  }
};

// `same;same`: an `Outer` and its `Inner` are cut from one CST record, so
// they share a symbol, and are still two nodes. The multi reference holds
// both; find-references lists their one declaration site and their one
// reference once each.
void two_nodes_on_one_record_are_two_targets() {
  cursiv::SharedCoreServices shared;
  shared.fileSystemProvider =
      std::make_unique<cursiv::EmptyFileSystemProvider>();
  cursiv::install_default_shared_core_services(shared);
  auto language = std::make_unique<cursiv::CoreServices>(shared);
  language->languageMetaData = {"sharedrecord", {".srm"}, {}};
  language->parser = tsr::generated::SharedRecord::create_parser();
  language->astReflection =
      tsr::generated::SharedRecord::create_ast_reflection();
  cursiv::install_default_core_services(*language);
  language->scopeComputation = std::make_unique<ExportPair>(*language);
  const cursiv::CoreServices* const srm = language.get();
  shared.serviceRegistry->registerServices(std::move(language));

  auto document =
      shared.documentFactory->fromString("same;same", "file:///same.srm");
  //                                      012345678
  cursiv::Document* const raw = document.get();
  shared.documents->addDocument(std::move(document));
  cursiv::Document* const set[] = {raw};
  shared.documentBuilder->build(set);

  const auto* const model = raw->parseResult.value_as<tsr::Model>();
  CHECK(model != nullptr && model->pair != nullptr &&
        model->pair->child != nullptr && model->use != nullptr);
  if (model == nullptr || model->pair == nullptr ||
      model->pair->child == nullptr || model->use == nullptr)
    return;
  const tsr::Outer* const outer = model->pair;
  const tsr::Inner* const inner = outer->child;
  CHECK(outer->cstNodeId == inner->cstNodeId);
  CHECK(raw->diagnostics.empty());

  const auto& target = model->use->target;
  CHECK(target.size() == 2);
  CHECK(target.get<tsr::Outer>(0) == outer);
  CHECK(target.get<tsr::Inner>(1) == inner);

  const cursiv::AstNode* const declared[] = {outer, inner};
  for (const cursiv::AstNode* const node : declared) {
    const std::vector<cursiv::ReferenceDescription> rows =
        srm->references->findReferences(*node, {.includeDeclaration = true});
    CHECK(rows.size() == 2);
    if (rows.size() != 2) continue;
    CHECK(rows[0].sourceBegin == 0 && rows[0].sourceEnd == 4);
    CHECK(rows[1].sourceBegin == 5 && rows[1].sourceEnd == 9);
  }
}

}  // namespace

void run() {
  the_grammar_confesses_its_multi_reference();
  navigation_uses_the_documents_language_id();
  navigation_resolves_a_reference_at_computed_scopes();
  a_declaration_stands_for_its_group(true);
  a_declaration_stands_for_its_group(false);
  the_group_does_not_leak_into_plain_references();
  two_documents_built_together_keep_their_own_group();
  a_multi_reference_tracks_new_global_candidates(false);
  a_multi_reference_tracks_new_global_candidates(true);
  a_throwing_provider_stays_on_its_multi_reference(false);
  a_throwing_provider_stays_on_its_multi_reference(true);
  two_nodes_on_one_record_are_two_targets();
}

}  // namespace references

// Pins the two symbol tables one document derives from its own tree: what it
// shows the world, and what is visible from inside it.
namespace scope_computation {

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
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    language->astReflection =
        ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    ws = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }

  // Stored, because a description names its document by id and the store is
  // what mints one: an unmanaged document describes nothing resolvable.
  std::shared_ptr<cursiv::Document> parse(std::string text) const {
    auto document = shared.documentFactory->fromString(std::move(text),
                                                       "file:///w/a.ws");
    shared.documents->addDocument(document);
    return document;
  }
};

bool has(const std::vector<cursiv::AstNodeDescription>& all,
         std::string_view name) {
  for (const cursiv::AstNodeDescription& d : all)
    if (d.name == name) return true;
  return false;
}

// A document shows the world its root's direct children -- not what is nested
// inside them.
void exported_symbols_are_the_top_level_ones() {
  const World world;
  const auto document =
      world.parse("datatype String\nentity Person { age : String }\n");
  const auto exported =
      world.ws->scopeComputation->collectExportedSymbols(*document);

  CHECK(has(exported, "String"));
  CHECK(has(exported, "Person"));
  // `age` is nested inside `Person`: another document cannot see it.
  CHECK(!has(exported, "age"));

  // Every description names its document and carries a live node.
  for (const cursiv::AstNodeDescription& d : exported) {
    CHECK(d.documentId == document->id);
    CHECK(d.symbolId != cursiv::kNoNode);
    CHECK(d.node != nullptr);
    CHECK(d.type != nullptr);
    CHECK(d.valid());
  }
}

// Inside, every named node is visible from its own container.
void local_symbols_are_bound_to_their_container() {
  const World world;
  const auto document = world.parse(
      "datatype String\nentity Person { age : String name2 : String }\n");
  const cursiv::LocalSymbols symbols =
      world.ws->scopeComputation->collectLocalSymbols(*document);

  CHECK(!symbols.empty());
  const cursiv::AstNode* const root = document->parseResult.value;
  // One name's entries at one container, through the sealed name table.
  const auto at = [&](const cursiv::AstNode* container, std::string_view name) {
    std::vector<const cursiv::AstNodeDescription*> found;
    symbols.find(container, name, cursiv::LocalSymbols::hash_name(name))
        .for_each([&](const cursiv::AstNodeDescription& entry) {
          found.push_back(&entry);
          return true;
        });
    return found;
  };

  CHECK(!symbols.forContainer(root).empty());
  CHECK(!at(root, "String").empty());
  CHECK(!at(root, "Person").empty());
  // `age` is not the root's: it belongs to the entity that contains it.
  CHECK(at(root, "age").empty());

  const cursiv::AstNodeDescription* const person = at(root, "Person").front();
  CHECK(!symbols.forContainer(person->node).empty());
  CHECK(!at(person->node, "age").empty());
  CHECK(!at(person->node, "name2").empty());

  // The document is nobody's scope: the root is not bound to it.
  CHECK(symbols.forContainer(document.get()).empty());
}

// A document with nothing named derives nothing, and does not fail.
void an_empty_document_derives_nothing() {
  const World world;
  const auto document = world.parse("");
  CHECK(world.ws->scopeComputation->collectLocalSymbols(*document).empty());
  // The root itself is unnamed, so even the export list is empty.
  CHECK(world.ws->scopeComputation->collectExportedSymbols(*document).empty());
}

void a_cancelled_walk_stops() {
  const World world;
  const auto document =
      world.parse("datatype String\nentity Person { age : String }\n");
  cursiv::CancellationTokenSource source;
  source.request_stop();

  bool threw = false;
  try {
    (void)world.ws->scopeComputation->collectLocalSymbols(*document,
                                                          source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

void run() {
  exported_symbols_are_the_top_level_ones();
  local_symbols_are_bound_to_their_container();
  an_empty_document_derives_nothing();
  a_cancelled_walk_stops();
}

}  // namespace scope_computation

// Pins what one reference site can see. The site and the name looked up in it
// are separate: a completion asks the same site for many prefixes.
namespace scope_provider {

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
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    cursiv::install_default_core_services(*language);
    ws = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }

  // A document taken to `ComputedScopes`, which is what a scope provider needs.
  std::shared_ptr<cursiv::Document> scoped(std::string text) const {
    auto document =
        shared.documentFactory->fromString(std::move(text), "file:///w/a.ws");
    shared.documents->addDocument(document);
    document->localSymbols =
        ws->scopeComputation->collectLocalSymbols(*document);
    document->state = cursiv::DocumentState::ComputedScopes;
    return document;
  }

  std::vector<std::string> visible(const cursiv::ReferenceInfo& context) const {
    std::vector<std::string> names;
    (void)ws->scopeProvider->visitScopeEntries(
        context, [&names](const cursiv::AstNodeDescription& entry) {
          names.emplace_back(entry.name);
          return true;
        });
    return names;
  }
};

cursiv::ReferenceInfo site(const cursiv::Document& document,
                           std::size_t which = 0) {
  const auto reflection = ws::generated::Workspace::create_ast_reflection();
  return static_cast<cursiv::DefaultAstReflection&>(*reflection)
      .referenceInfo(document.parseResult.references->handles[which]);
}

void a_site_describes_itself_without_a_name() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  CHECK(!document->parseResult.references->handles.empty());

  const cursiv::ReferenceInfo context = site(*document);
  CHECK(context.container != nullptr);
  CHECK(context.property == "type");
  CHECK(!context.index.has_value());
  CHECK(context.reference != cursiv::kNoReference);

  const cursiv::AstNodeDescription* const found =
      world.ws->scopeProvider->getScopeEntry(context, "String");
  CHECK(found != nullptr);
  CHECK(found->name == "String");
  CHECK(found->node != nullptr);
}

// The same site, many prefixes -- which is what a content assist does, and
// why the name is not part of the site. A site's index is the key to
// everything the grammar knows about the member.
void a_site_index_says_what_the_member_is() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  const cursiv::ReferenceInfo context = site(*document);
  const cursiv::AstReflection& reflection = *world.ws->astReflection;

  // `Feature::type` is a single `reference<Type>`, not a multi one.
  CHECK(!reflection.isMultiReference(context.reference));

  // What it declares: one type, `Type` -- the base both families derive from.
  const std::span<const std::uint16_t> targets =
      reflection.referenceTargets(context.reference);
  CHECK(targets.size() == 1);
  CHECK(reflection.typeTagOf(targets.front()) != nullptr);

  // What it accepts resolves the subtypes: a DataType and an Entity both pass,
  // and a Feature does not.
  const std::span<const std::uint64_t> accepts =
      reflection.referenceAccepts(context.reference);
  const auto indexOf = [&](const cursiv::AstTypeTag* tag) {
    return reflection.typeIndexOf(tag);
  };
  CHECK(cursiv::AstReflection::accepts(
      accepts, indexOf(&cursiv::ast_type_tag<ws::ast::DataType>)));
  CHECK(cursiv::AstReflection::accepts(
      accepts, indexOf(&cursiv::ast_type_tag<ws::ast::Entity>)));
  CHECK(!cursiv::AstReflection::accepts(
      accepts, indexOf(&cursiv::ast_type_tag<ws::ast::Feature>)));

  // And nothing at all is a site that filters nothing.
  CHECK(cursiv::AstReflection::accepts(
      reflection.referenceAccepts(cursiv::kNoReference), 0));
}

void one_site_answers_many_names() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  const cursiv::ReferenceInfo context = site(*document);

  CHECK(world.ws->scopeProvider->getScopeEntry(context, "String") != nullptr);
  CHECK(world.ws->scopeProvider->getScopeEntry(context, "Person") != nullptr);
  CHECK(world.ws->scopeProvider->getScopeEntry(context, "Str") == nullptr);
  CHECK(world.ws->scopeProvider->getScopeEntry(context, "") == nullptr);
}

// A site stated by hand: a container, the member's type. Nothing of the engine.
void a_site_can_be_filled_by_hand() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  const cursiv::ReferenceInfo real = site(*document);

  // A caller holding only the member's type asks the reflection for its row --
  // the one lookup, done once.
  cursiv::ReferenceInfo typed;
  typed.container = real.container;
  typed.property = "type";
  typed.reference = world.ws->astReflection->referenceIndexOf(
      &cursiv::ast_type_tag<cursiv::reference<ws::ast::Type>>);
  CHECK(typed.reference == real.reference);
  CHECK(world.ws->scopeProvider->getScopeEntry(typed, "String") != nullptr);
  CHECK(!world.visible(typed).empty());
}

// `Feature::type` is `reference<DataType, Entity>`: both families resolve. A
// filter naming only the first would silently refuse the second.
void the_whole_candidate_set_is_honoured() {
  const World world;
  const auto document = world.scoped(
      "datatype String\nentity A { }\nentity B { f : A g : String }\n");
  CHECK(document->parseResult.references->handles.size() == 2);

  for (std::size_t i = 0; i < 2; ++i) {
    const cursiv::RefHandle& handle =
        document->parseResult.references->handles[i];
    const cursiv::ReferenceInfo context = site(*document, i);
    const std::string_view text =
        static_cast<const cursiv::DefaultAstReflection&>(
            *world.ws->astReflection)
            .metaData()
            .refOps[handle.field]
            .text(*handle.owner, handle.index);
    const cursiv::AstNodeDescription* const found =
        world.ws->scopeProvider->getScopeEntry(context, text);
    CHECK(found != nullptr);
    CHECK(found->name == text);
  }
}

// A candidate the reference cannot hold is not in its scope, however close.
void the_type_filter_skips_the_homonyms() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity E { String : String }\n");
  const cursiv::ReferenceInfo context = site(*document);

  const std::vector<std::string> names = world.visible(context);
  CHECK(names.size() == 2);  // the datatype and the entity, not the member

  const cursiv::AstNodeDescription* const found =
      world.ws->scopeProvider->getScopeEntry(context, "String");
  CHECK(found != nullptr);
  // The top-level declaration, the nearer homonym having been refused.
  CHECK(found->node->parent == document->parseResult.value);
}

// Innermost first, once nothing filters.
void the_walk_goes_from_the_site_outwards() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity E { String : String }\n");
  cursiv::ReferenceInfo context = site(*document);
  context.reference = cursiv::kNoReference;  // an unfiltered enumeration

  const std::vector<std::string> names = world.visible(context);
  CHECK(names.size() == 3);
  CHECK(names.front() == "String");  // the member, innermost

  const cursiv::AstNodeDescription* const first =
      world.ws->scopeProvider->getScopeEntry(context, "String");
  CHECK(first != nullptr);
  CHECK(first->node->parent != document->parseResult.value);
}

void a_visitor_can_stop_the_walk() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : String }\n");
  cursiv::ReferenceInfo context = site(*document);
  context.reference = cursiv::kNoReference;

  std::size_t seen = 0;
  const bool ranToEnd = world.ws->scopeProvider->visitScopeEntries(
      context, [&seen](const cursiv::AstNodeDescription&) {
        ++seen;
        return false;
      });
  CHECK(seen == 1);
  CHECK(!ranToEnd);
}

void a_detached_site_sees_nothing() {
  const World world;
  const cursiv::ReferenceInfo detached;
  CHECK(world.ws->scopeProvider->getScopeEntry(detached, "x") == nullptr);
  CHECK(world.visible(detached).empty());
}

// The one override a case-insensitive language writes.
struct CaseInsensitiveProvider final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;

 protected:
  [[nodiscard]] bool isCaseSensitive(
      const cursiv::ReferenceInfo&) const noexcept override {
    return false;
  }
};

void the_policy_defaults_to_case_sensitive() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : StrinG }\n");
  const cursiv::ReferenceInfo context = site(*document);
  CHECK(world.ws->scopeProvider->getScopeEntry(context, "StrinG") == nullptr);
  CHECK(world.ws->scopeProvider->getScopeEntry(context, "String") != nullptr);
}

void an_insensitive_provider_matches_across_case() {
  const World world;
  const auto document =
      world.scoped("datatype String\nentity Person { age : StrinG }\n");
  const cursiv::ReferenceInfo context = site(*document);
  const CaseInsensitiveProvider provider(*world.ws);

  const cursiv::AstNodeDescription* const found =
      provider.getScopeEntry(context, "StrinG");
  CHECK(found != nullptr);
  CHECK(found->name == "String");  // the spelling stays the author's
}

// Shadowing follows the policy: the closest fold-equal declaration wins,
// whatever its case -- exactly what an exact name does today.
void shadowing_respects_the_policy() {
  const World world;
  const auto document = world.scoped(
      "datatype STRING\n"
      "package p {\n  datatype String\n  entity E { f : string }\n}\n");
  const cursiv::ReferenceInfo context = site(*document);
  const CaseInsensitiveProvider insensitive(*world.ws);

  const cursiv::AstNodeDescription* const inner =
      insensitive.getScopeEntry(context, "string");
  CHECK(inner != nullptr);
  CHECK(inner->name == "String");  // the package's, not the root's
  CHECK(world.ws->scopeProvider->getScopeEntry(context, "string") == nullptr);
}

// One container, two spellings of one name: an exact reader passes the first
// when it only folds to the name, and a folded reader takes it.
void an_exact_name_passes_a_folded_homonym() {
  const World world;
  const auto document = world.scoped(
      "datatype string\ndatatype String\nentity E { f : String }\n");
  const cursiv::ReferenceInfo context = site(*document);
  const CaseInsensitiveProvider insensitive(*world.ws);

  const cursiv::AstNodeDescription* const exact =
      world.ws->scopeProvider->getScopeEntry(context, "String");
  CHECK(exact != nullptr && exact->name == "String");
  const cursiv::AstNodeDescription* const folded =
      insensitive.getScopeEntry(context, "String");
  CHECK(folded != nullptr && folded->name == "string");
}

// The named visit answers what `getScopeEntry` answers first, under both
// policies -- `getCandidate` and `getCandidates` read the same walk.
void the_named_visit_leads_with_the_entry() {
  const World world;
  const auto document = world.scoped(
      "datatype STRING\n"
      "package p {\n  datatype String\n  entity E { f : string }\n}\n");
  const cursiv::ReferenceInfo context = site(*document);
  const CaseInsensitiveProvider insensitive(*world.ws);

  std::vector<std::string> matched;
  CHECK(insensitive.visitScopeEntries(
      context, "string", [&](const cursiv::AstNodeDescription& entry) {
        matched.emplace_back(entry.name);
        return true;
      }));
  CHECK(!matched.empty());
  CHECK(matched.front() == insensitive.getScopeEntry(context, "string")->name);

  matched.clear();
  CHECK(world.ws->scopeProvider->visitScopeEntries(
      context, "STRING", [&](const cursiv::AstNodeDescription& entry) {
        matched.emplace_back(entry.name);
        return true;
      }));
  for (const std::string& name : matched) CHECK(name == "STRING");
  CHECK(matched.front() ==
        world.ws->scopeProvider->getScopeEntry(context, "STRING")->name);

  // An empty name names nothing, in both shapes.
  CHECK(insensitive.getScopeEntry(context, "") == nullptr);
  CHECK(insensitive.visitScopeEntries(
      context, "", [](const cursiv::AstNodeDescription&) { return false; }));
}

// The no-name door enumerates: what the site currently spells is a fact about
// the text, not a filter on what may stand there -- filtering by it would hide
// every other completion.
void the_no_name_door_ignores_what_the_site_spells() {
  const World world;
  const auto document = world.scoped(
      "datatype String\ndatatype Number\nentity Person { age : String }\n");
  world.shared.indexManager->updateContent(*document);

  cursiv::ReferenceInfo context = site(*document);
  context.referenceText = "String";
  const std::vector<std::string> names = world.visible(context);
  // Once from the local climb, once from the global index: a spelled site
  // must not shrink the global arm to its own text.
  CHECK(std::ranges::count(names, std::string("Number")) == 2);
  CHECK(std::ranges::count(names, std::string("String")) == 2);
}

}  // namespace

void run() {
  a_site_describes_itself_without_a_name();
  a_site_index_says_what_the_member_is();
  one_site_answers_many_names();
  a_site_can_be_filled_by_hand();
  the_whole_candidate_set_is_honoured();
  the_type_filter_skips_the_homonyms();
  the_walk_goes_from_the_site_outwards();
  a_visitor_can_stop_the_walk();
  a_detached_site_sees_nothing();
  the_policy_defaults_to_case_sensitive();
  an_insensitive_provider_matches_across_case();
  shadowing_respects_the_policy();
  an_exact_name_passes_a_folded_homonym();
  the_named_visit_leads_with_the_entry();
  the_no_name_door_ignores_what_the_site_spells();
}

}  // namespace scope_provider

int main() {
  cursiv::test::count_expectations();
  linker::run();
  name_provider::run();
  references::run();
  scope_computation::run();
  scope_provider::run();
  return cursiv::test::report("references");
}
