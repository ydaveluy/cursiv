#include "cursiv/core/workspace/DefaultIndexManager.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/core/references/ScopeComputation.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/CaseFold.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/core/workspace/ReferenceDescriptionProvider.hpp"

namespace cursiv {

namespace {

// The document's own language when the factory resolved one, else what the
// registry says of its URI and buffer: null skips the document, it never fails
// the phase.
const CoreServices* language_of(const SharedCoreServices& shared,
                                const Document& document) {
  if (document.language != nullptr) return document.language;
  return shared.serviceRegistry->findServices(
      document.uri, document.textDocument().languageId());
}

// At most half full, so a probe stays short and always meets a free slot.
std::size_t table_size(std::size_t entries) noexcept {
  return std::bit_ceil(std::max<std::size_t>(entries * 2, 1));
}

}  // namespace

std::size_t DefaultIndexManager::Exports::Shard::slotOf(
    std::uint64_t hash) const noexcept {
  const auto mask = slots.size() - 1;
  auto at = static_cast<std::size_t>(hash) & mask;
  while (slots[at].count != 0 && slots[at].hash != hash) at = (at + 1) & mask;
  return at;
}

std::span<const AstNodeDescription* const> DefaultIndexManager::Exports::named(
    std::string_view name) const {
  const auto hash = folded_hash(name);
  auto& shard = shards[shardOf(hash)];
  if (!shard.built.load(std::memory_order_acquire)) {
    const std::scoped_lock lock(shard.building);
    if (!shard.built.load(std::memory_order_relaxed)) {
      fill(shard, shardOf(hash));
      shard.built.store(true, std::memory_order_release);
    }
  }
  const auto& slot = shard.slots[shard.slotOf(hash)];
  return {shard.byName.data() + slot.begin, slot.count};
}

// One pass counts each run, the next fills them back to front, so that every
// run preserves the snapshot's document and export order.
void DefaultIndexManager::Exports::fill(Shard& shard, std::size_t at) const {
  const auto group = [at](const Exported& exported) {
    return std::span(exported.byShard)
        .subspan(exported.shardBegin[at],
                 exported.shardBegin[at + 1] - exported.shardBegin[at]);
  };
  std::size_t count = 0;
  for (const auto& [id, exported] : rows) count += group(*exported).size();
  shard.slots.assign(table_size(count), {});
  for (const auto& [id, exported] : rows)
    for (const auto& entry : group(*exported)) {
      auto& slot = shard.slots[shard.slotOf(entry.hash)];
      slot.hash = entry.hash;
      ++slot.count;
    }
  std::uint32_t end = 0;
  for (auto& slot : shard.slots) slot.begin = end += slot.count;
  shard.byName.resize(count);
  for (const auto& [id, exported] : rows | std::views::reverse)
    for (const auto& entry : group(*exported) | std::views::reverse)
      shard.byName[--shard.slots[shard.slotOf(entry.hash)].begin] =
          entry.element;
}

std::size_t DefaultIndexManager::TargetHash::operator()(
    const Target& t) const noexcept {
  const std::size_t d =
      std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(t.document));
  const std::size_t s =
      std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(t.symbol));
  return d ^ (s + 0x9e3779b9u + (d << 6u) + (d >> 2u));
}

void DefaultIndexManager::updateContent(const Document& document,
                                        const CancellationToken& cancelToken) {
  const CoreServices* const language = language_of(shared, document);
  if (language == nullptr) return;
  // The index computes its descriptions rather than receiving them: handed a
  // ready-made vector it would move the work up into the phase and lose the
  // token. Computed off the lock -- this walk is the phase's whole cost.
  std::vector<AstNodeDescription> exported =
      language->scopeComputation->collectExportedSymbols(document, cancelToken);

  auto fresh = std::make_shared<Exported>();
  fresh->uri = document.uri;
  fresh->elements = std::move(exported);
  std::vector<Hashed> hashed;
  hashed.reserve(fresh->elements.size());
  for (const auto& element : fresh->elements) {
    hashed.push_back({folded_hash(element.name), &element});
    ++fresh->shardBegin[shardOf(hashed.back().hash) + 1];
  }
  std::partial_sum(fresh->shardBegin.begin(), fresh->shardBegin.end(),
                   fresh->shardBegin.begin());
  fresh->byShard.resize(hashed.size());
  auto next = fresh->shardBegin;
  for (const auto& entry : hashed)
    fresh->byShard[next[shardOf(entry.hash)]++] = entry;
  const std::scoped_lock lock(_mutex);
  if (const auto old = _exported.find(document.id); old != _exported.end())
    affectImporters(*old->second);
  affectImporters(*fresh);
  _exported.insert_or_assign(document.id, std::move(fresh));
  // What a reader already holds stays valid; the next one rebuilds. Only the
  // export view: nothing here changes what points at what.
  _exports.retire();
}

void DefaultIndexManager::updateReferences(
    const Document& document, const CancellationToken& cancelToken) {
  throw_if_cancelled(cancelToken);
  // What the document points at is the language's answer, not the index's: a
  // grammar whose references are not readable off the work list installs its
  // own provider and this loop does not change.
  const CoreServices* const language = language_of(shared, document);
  if (language == nullptr) return;

  // Collected off the lock, as the exports are.
  Referenced referenced;
  referenced.descriptions =
      language->referenceDescriptionProvider->createDescriptions(document,
                                                                 cancelToken);
  for (const auto& reference : referenced.descriptions)
    if (!reference.local)
      referenced.targets |= std::uint64_t{1}
                            << (static_cast<std::uint32_t>(
                                    reference.targetDocumentId) & 63);
  const auto* references = document.parseResult.references.get();
  auto names = references == nullptr ? std::vector<std::uint64_t>{}
                                      : references->importedNames.snapshot();

  const std::scoped_lock lock(_mutex);
  dropReferences(document.id);
  if (!names.empty()) {
    auto& imports = _importedNames[document.id];
    imports.reserve(names.size());
    for (const auto name : names) {
      auto& importers = _importers[name];
      importers.push_back({document.id,
                           static_cast<std::uint32_t>(imports.size())});
      imports.push_back(
          {name, static_cast<std::uint32_t>(importers.size() - 1)});
    }
  }
  _references.insert_or_assign(document.id, std::move(referenced));
  _affected.erase(document.id);
  // The linker is reading the export view while this phase runs; dropping it
  // here would rebuild it once per document.
  _incoming.retire();
}

bool DefaultIndexManager::removeContent(DocumentId document) {
  const std::scoped_lock lock(_mutex);
  return dropContent(document);
}

bool DefaultIndexManager::removeReferences(DocumentId document) {
  const std::scoped_lock lock(_mutex);
  return dropReferences(document);
}

// One lock for the pair, which is why the two halves are dropped through the
// unlocked forms: `std::mutex` is not recursive, and taking it twice on one
// thread is the deadlock this shape exists to prevent.
bool DefaultIndexManager::remove(DocumentId document) {
  const std::scoped_lock lock(_mutex);
  const bool content = dropContent(document);
  const bool references = dropReferences(document);
  return content || references;
}

bool DefaultIndexManager::dropContent(DocumentId document) {
  if (const auto old = _exported.find(document); old != _exported.end())
    affectImporters(*old->second);
  _affected.erase(document);
  const bool had = _exported.erase(document) != 0;
  if (had) _exports.retire();
  return had;
}

bool DefaultIndexManager::dropReferences(DocumentId document) {
  if (const auto imported = _importedNames.find(document);
      imported != _importedNames.end()) {
    for (const auto& entry : imported->second) {
      const auto at = _importers.find(entry.name);
      auto& importers = at->second;
      const auto last = importers.back();
      importers[entry.position] = last;
      importers.pop_back();
      // Back-pointers avoid quadratic erasure of a widely imported name.
      if (entry.position < importers.size())
        _importedNames.at(last.document)[last.nameIndex].position = entry.position;
      if (importers.empty()) _importers.erase(at);
    }
    _importedNames.erase(imported);
  }
  const bool had = _references.erase(document) != 0;
  if (had) _incoming.retire();
  return had;
}

void DefaultIndexManager::affectImporters(const Exported& exports) {
  if (_importers.empty()) return;
  for (const auto& entry : exports.byShard)
    if (const auto at = _importers.find(entry.hash); at != _importers.end())
      for (const auto& importer : at->second)
        _affected.insert(importer.document);
}

bool DefaultIndexManager::isAffected(
    const Document& document,
    const std::unordered_set<DocumentId>& changed) const {
  const std::scoped_lock lock(_mutex);
  if (_affected.contains(document.id)) return true;
  if (changed.empty()) return false;
  const auto it = _references.find(document.id);
  if (it == _references.end() || it->second.targets == 0) return false;
  // A disjoint mask rules out every target; collisions still need exact IDs.
  // A changed set larger than the edges is cheaper to meet edge by edge.
  const auto& edges = it->second.descriptions;
  if (changed.size() < edges.size() &&
      std::ranges::none_of(changed, [&](DocumentId id) {
        return (it->second.targets &
                (std::uint64_t{1} << (static_cast<std::uint32_t>(id) & 63))) !=
               0;
      }))
    return false;
  // A local edge is the document's own: its change resets it whole, and it
  // does not affect itself.
  for (const ReferenceDescription& reference : edges)
    if (!reference.local && changed.contains(reference.targetDocumentId))
      return true;
  return false;
}

// No lock across the visitor: it is the caller's code and it reaches back into
// this index, which a held lock would deadlock on.
bool DefaultIndexManager::visitElements(
    const AstTypeTag* type, std::span<const DocumentId> documents,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  const auto view = exportsView();
  const auto wanted = [&](DocumentId id) {
    return documents.empty() ||
           std::ranges::find(documents, id) != documents.end();
  };
  for (const auto& [id, elements] : view->rows) {
    if (!wanted(id)) continue;
    for (const AstNodeDescription& element : elements->elements) {
      if (type != nullptr &&
          (element.type == nullptr || !element.type->derivesFrom(type)))
        continue;
      if (!visitor(element)) return false;
    }
  }
  return true;
}

const AstNodeDescription* DefaultIndexManager::findByName(
    std::string_view name, bool caseSensitive, AcceptSet accepts,
    const AstReflection& reflection) const {
  if (name.empty()) return nullptr;
  const auto view = exportsView();
  for (const auto* const entry : view->named(name)) {
    if (caseSensitive ? entry->name != name : !folded_equal(entry->name, name))
      continue;
    if (reflection.acceptsTag(accepts, entry->type)) return entry;
  }
  return nullptr;
}

bool DefaultIndexManager::visitByName(
    std::string_view name, bool caseSensitive, AcceptSet accepts,
    const AstReflection& reflection,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  if (name.empty()) return true;
  const auto view = exportsView();
  for (const auto* const entry : view->named(name)) {
    if (caseSensitive ? entry->name != name : !folded_equal(entry->name, name))
      continue;
    if (reflection.acceptsTag(accepts, entry->type) && !visitor(*entry))
      return false;
  }
  return true;
}

bool DefaultIndexManager::visitReferences(
    DocumentId targetDocument, NodeId targetSymbol,
    cursiv::function_ref<bool(const ReferenceDescription&)> visitor) const {
  if (targetDocument == kNoDocument || targetSymbol == kNoNode) return true;
  // The guard stands for the whole walk: it is what keeps the snapshot from
  // being released under the visitor.
  const auto view = incomingView();
  const auto it = view->byTarget.find(Target{targetDocument, targetSymbol});
  if (it == view->byTarget.end()) return true;
  for (const ReferenceDescription& row : it->second)
    if (!visitor(row)) return false;
  return true;
}

cursiv::Published<DefaultIndexManager::Exports>::Read
DefaultIndexManager::exportsView() const {
  if (auto live = _exports.read(); live) return live;

  const std::scoped_lock lock(_mutex);
  // Another reader may have published while this one waited.
  if (auto live = _exports.read(); live) return live;

  auto built = std::make_shared<Exports>();
  built->rows.assign(_exported.begin(), _exported.end());
  // Normalized URI order keeps homonyms independent of worker completion and
  // document registration. Sort the snapshot once, never individual lookups.
  std::ranges::sort(built->rows, {},
                    [](const auto& row) -> const std::string& {
                      return row.second->uri;
                    });
  _exports.publish(std::move(built));
  return _exports.read();
}

// Only what resolved is stored, so nothing is filtered again here.
cursiv::Published<DefaultIndexManager::Incoming>::Read
DefaultIndexManager::incomingView() const {
  if (auto live = _incoming.read(); live) return live;

  const std::scoped_lock lock(_mutex);
  if (auto live = _incoming.read(); live) return live;

  auto built = std::make_shared<Incoming>();
  for (const auto& [id, references] : _references)
    for (const ReferenceDescription& reference : references.descriptions)
      built
          ->byTarget[Target{reference.targetDocumentId,
                            reference.targetSymbolId}]
          .push_back(reference);
  // In source order, whatever order the table hashed the documents into: a
  // reader that takes the first row that fits must find the same one twice.
  for (auto& [target, rows] : built->byTarget)
    std::ranges::sort(rows, {}, [](const ReferenceDescription& r) {
      return std::tuple{r.sourceDocumentId, r.sourceBegin, r.sourceEnd};
    });
  _incoming.publish(std::move(built));
  return _incoming.read();
}

}  // namespace cursiv
