#include "cursiv/workspace/DefaultIndexManager.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/references/ScopeComputation.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"
#include "cursiv/workspace/ReferenceDescriptionProvider.hpp"

namespace cursiv {

namespace {


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
  // A run's bounds are 32 bits: a shard too large is refused before it is
  // built.
  if (count > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("cursiv: too many exported names in one shard");
  shard.slots.assign(detail::table_size(count), {});
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

void DefaultIndexManager::updateContent(const Document& document,
                                        const CancellationToken& cancelToken) {
  const CoreServices* const language = document.language;
  if (language == nullptr) return;
  // Computed here, off the lock and with the token: this walk is the phase's
  // whole cost.
  std::vector<AstNodeDescription> exported =
      language->scopeComputation->collectExportedSymbols(document, cancelToken);

  // The shard bounds of one document are 32 bits.
  if (exported.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("cursiv: too many exported names in one document");
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
  // Released first, so a throw leaves the table as the view shows it; readers
  // keep what they hold, and the next one rebuilds. Only the export view.
  _exports.clear();
  if (const auto old = _exported.find(document.id); old != _exported.end())
    affectImporters(*old->second);
  affectImporters(*fresh);
  _exported.insert_or_assign(document.id, std::move(fresh));
}

void DefaultIndexManager::updateReferences(
    const Document& document, const CancellationToken& cancelToken) {
  throw_if_cancelled(cancelToken);
  // The language's own provider says what the document points at.
  const CoreServices* const language = document.language;
  if (language == nullptr) return;

  // Collected off the lock, as the exports are.
  Referenced referenced;
  referenced.descriptions =
      language->referenceDescriptionProvider->createDescriptions(document,
                                                                 cancelToken);
  for (const auto& reference : referenced.descriptions)
    if (!reference.local &&
        (referenced.targets.empty() ||
         referenced.targets.back() != reference.targetDocumentId))
      referenced.targets.push_back(reference.targetDocumentId);
  std::ranges::sort(referenced.targets);
  referenced.targets.erase(std::ranges::unique(referenced.targets).begin(),
                           referenced.targets.end());
  const auto* references = document.parseResult.references.get();
  auto names = references == nullptr ? std::vector<std::uint64_t>{}
                                      : references->importedNames.snapshot();
  auto row = std::make_shared<const Referenced>(std::move(referenced));

  const std::scoped_lock lock(_mutex);
  // Before the tables change: the incoming view keeps the row it was built
  // from, or notes that the document had none.
  (void)dropReferences(document.id);
  noteIncoming(document.id);
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
  _references.insert_or_assign(document.id, std::move(row));
  _affected.erase(document.id);
  // The export view stays: the linker is reading it while this phase runs, and
  // dropping it here would rebuild it once per document.
}

bool DefaultIndexManager::removeContent(DocumentId document) {
  const std::scoped_lock lock(_mutex);
  return dropContent(document);
}

bool DefaultIndexManager::removeReferences(DocumentId document) {
  const std::scoped_lock lock(_mutex);
  return dropReferences(document);
}

// One lock for both halves, dropped through the unlocked forms: `std::mutex`
// is not recursive.
bool DefaultIndexManager::remove(DocumentId document) {
  const std::scoped_lock lock(_mutex);
  const bool content = dropContent(document);
  const bool references = dropReferences(document);
  return content || references;
}

// Each view is released before its table loses the row: a release that
// throws leaves the row there, and the next removal tries again.
bool DefaultIndexManager::dropContent(DocumentId document) {
  const auto old = _exported.find(document);
  if (old != _exported.end()) {
    _exports.clear();
    affectImporters(*old->second);
  }
  _affected.erase(document);
  if (old == _exported.end()) return false;
  _exported.erase(old);
  return true;
}

bool DefaultIndexManager::dropReferences(DocumentId document) {
  const auto old = _references.find(document);
  if (old != _references.end()) noteIncoming(document);
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
  if (old == _references.end()) return false;
  _references.erase(old);
  return true;
}

void DefaultIndexManager::noteIncoming(DocumentId source) {
  ++_referenceWrites;
  _incomingStale.store(true, std::memory_order_release);
  if (_lastIncoming == nullptr) return;
  const auto row = _references.find(source);
  // The first change since the view keeps the row the view was built from.
  _changedSources.try_emplace(source,
                              row != _references.end() ? row->second : nullptr);
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
  if (it == _references.end()) return false;
  // Its own change resets it whole: only the other documents it points into
  // count, and the smaller of the two sets is the one walked.
  const std::vector<DocumentId>& targets = it->second->targets;
  if (changed.size() < targets.size())
    return std::ranges::any_of(changed, [&](DocumentId id) {
      return std::ranges::binary_search(targets, id);
    });
  return std::ranges::any_of(
      targets, [&](DocumentId id) { return changed.contains(id); });
}

// No lock across the visitor: it is the caller's code and it reaches back into
// this index, which a held lock would deadlock on.
bool DefaultIndexManager::visitElements(
    const AstTypeTag* type, std::span<const DocumentId> documents,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  const auto view = exportsView();
  const auto visit = [&](const Exported& exported) {
    for (const AstNodeDescription& element : exported.elements) {
      if (type != nullptr &&
          (element.type == nullptr || !element.type->derivesFrom(type)))
        continue;
      if (!visitor(element)) return false;
    }
    return true;
  };
  if (documents.empty())
    return std::ranges::all_of(
        view->rows, [&](const auto& row) { return visit(*row.second); });
  // The documents asked alone, in the view's order.
  std::vector<std::uint32_t> rows;
  rows.reserve(documents.size());
  for (const DocumentId id : documents)
    if (const auto at = view->rowAt.find(id); at != view->rowAt.end())
      rows.push_back(at->second);
  std::ranges::sort(rows);
  rows.erase(std::ranges::unique(rows).begin(), rows.end());
  return std::ranges::all_of(
      rows, [&](std::uint32_t at) { return visit(*view->rows[at].second); });
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
  return std::ranges::all_of(view->named(name), [&](const auto* entry) {
    if (caseSensitive ? entry->name != name : !folded_equal(entry->name, name))
      return true;
    return !reflection.acceptsTag(accepts, entry->type) || visitor(*entry);
  });
}

bool DefaultIndexManager::visitReferences(
    DocumentId targetDocument, NodeId targetSymbol,
    cursiv::function_ref<bool(const ReferenceDescription&)> visitor) const {
  if (targetDocument == kNoDocument || targetSymbol == kNoNode) return true;
  // The guard stands for the whole walk: it is what keeps the snapshot from
  // being released under the visitor.
  const auto view = incomingView();
  const auto document = view->byDocument.find(targetDocument);
  if (document == view->byDocument.end()) return true;
  const auto symbol = document->second->find(targetSymbol);
  if (symbol == document->second->end()) return true;
  return std::ranges::all_of(*symbol->second, visitor);
}

cursiv::detail::Published<DefaultIndexManager::Exports>::Read
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
  built->rowAt.reserve(built->rows.size());
  for (std::uint32_t at = 0; at < built->rows.size(); ++at)
    built->rowAt.emplace(built->rows[at].first, at);
  _exports.publish(std::move(built));
  return _exports.read();
}

namespace {

// What a source points at, in position order.
void sort_by_position(std::vector<ReferenceDescription>& list) {
  constexpr auto at = [](const ReferenceDescription& r) {
    return std::tuple{r.sourceDocumentId, r.sourceBegin, r.sourceEnd};
  };
  if (!std::ranges::is_sorted(list, {}, at)) std::ranges::sort(list, {}, at);
}

}  // namespace

// Only what resolved is stored, so nothing is filtered again here. Every list
// is in source order, whatever order the tables hashed the documents into: a
// reader that takes the first row that fits must find the same one twice.
DefaultIndexManager::IncomingView DefaultIndexManager::incomingView() const {
  if (!_incomingStale.load(std::memory_order_acquire))
    if (auto live = _incoming.read(); live)
      return IncomingView(std::move(live));

  // The rows are taken under the lock and grouped after it, so writers do not
  // wait for them: every row for a first view, else the changed sources' rows
  // then and now.
  using Row = std::shared_ptr<const Referenced>;
  struct Change {
    DocumentId source;
    Row then;
    Row now;
  };
  std::shared_ptr<const Incoming> last;
  std::vector<Change> changes;
  std::uint64_t writes = 0;
  {
    const std::scoped_lock lock(_mutex);
    if (!_incomingStale.load(std::memory_order_relaxed))
      if (auto live = _incoming.read(); live)
        return IncomingView(std::move(live));
    last = _lastIncoming;
    if (last == nullptr) {
      for (const auto& [source, row] : _references)
        changes.push_back({source, nullptr, row});
    } else {
      for (const auto& [source, then] : _changedSources) {
        const auto now = _references.find(source);
        changes.push_back(
            {source, then, now != _references.end() ? now->second : nullptr});
      }
    }
    writes = _referenceWrites;
  }
  std::ranges::sort(changes, {}, &Change::source);

  // Per symbol, each changed source with what it points there now, in source
  // order: a source that pointed there only then has an empty list.
  std::unordered_map<
      DocumentId, std::unordered_map<
                      NodeId, std::vector<std::pair<DocumentId, IncomingRows>>>>
      touched;
  for (const Change& change : changes) {
    const auto slot = [&](const ReferenceDescription& r) -> IncomingRows& {
      auto& list = touched[r.targetDocumentId][r.targetSymbolId];
      if (list.empty() || list.back().first != change.source)
        list.emplace_back(change.source, IncomingRows{});
      return list.back().second;
    };
    if (change.then != nullptr)
      for (const ReferenceDescription& r : change.then->descriptions)
        (void)slot(r);
    if (change.now != nullptr)
      for (const ReferenceDescription& r : change.now->descriptions)
        slot(r).push_back(r);
  }

  auto built = std::make_shared<Incoming>();
  if (last != nullptr) built->byDocument = last->byDocument;
  for (auto& [document, symbols] : touched) {
    const auto held = built->byDocument.find(document);
    auto entry = held != built->byDocument.end()
                     ? std::make_shared<IncomingSymbols>(*held->second)
                     : std::make_shared<IncomingSymbols>();
    for (auto& [symbol, sources] : symbols) {
      const auto before = entry->find(symbol);
      const std::span<const ReferenceDescription> old =
          before != entry->end() ? std::span(*before->second)
                                 : std::span<const ReferenceDescription>{};
      // The references of every other source stay where they were.
      auto into = std::make_shared<IncomingRows>();
      auto o = old.begin();
      for (auto& [source, now] : sources) {
        for (; o != old.end() && o->sourceDocumentId < source; ++o)
          into->push_back(*o);
        while (o != old.end() && o->sourceDocumentId == source) ++o;
        sort_by_position(now);
        into->insert(into->end(), now.begin(), now.end());
      }
      into->insert(into->end(), o, old.end());
      if (into->empty())
        entry->erase(symbol);
      else
        entry->insert_or_assign(symbol, std::move(into));
    }
    if (entry->empty())
      built->byDocument.erase(document);
    else
      built->byDocument.insert_or_assign(document, std::move(entry));
  }

  const std::scoped_lock lock(_mutex);
  // A write since the rows were taken: this view is still right for this
  // reader, but is not published.
  if (writes != _referenceWrites) return IncomingView(std::move(built));
  _incoming.publish(built);
  _lastIncoming = std::move(built);
  _changedSources.clear();
  _incomingStale.store(false, std::memory_order_release);
  return IncomingView(_incoming.read());
}

}  // namespace cursiv
