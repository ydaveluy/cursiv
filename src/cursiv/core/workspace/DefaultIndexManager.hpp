#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cursiv/core/services/DefaultSharedCoreService.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/utils/Published.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"

// One row per document, so removing one is dropping its row: an index keyed the
// other way would have to scan everything a document ever exported.

namespace cursiv {

// Not `final`. Everything below stays private -- the two
// tables are copy-on-write behind a lock, and a derived class reaching into
// them would break the discipline that makes a reader lock-free.
class DefaultIndexManager : public IndexManager,
                            protected DefaultSharedCoreService {
 public:
  using DefaultSharedCoreService::DefaultSharedCoreService;

  void updateContent(const Document& document,
                     const CancellationToken& cancelToken = kNoCancel) override;
  void updateReferences(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) override;

  bool removeContent(DocumentId document) override;
  bool removeReferences(DocumentId document) override;
  bool remove(DocumentId document) override;

  [[nodiscard]] bool isAffected(
      const Document& document,
      const std::unordered_set<DocumentId>& changed) const override;

  [[nodiscard]] bool visitElements(
      const AstTypeTag* type, std::span<const DocumentId> documents,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor)
      const override;

  [[nodiscard]] const AstNodeDescription* findByName(
      std::string_view name, bool caseSensitive, AcceptSet accepts,
      const AstReflection& reflection) const override;

  [[nodiscard]] bool visitByName(
      std::string_view name, bool caseSensitive, AcceptSet accepts,
      const AstReflection& reflection,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor)
      const override;

  [[nodiscard]] bool visitReferences(
      DocumentId targetDocument, NodeId targetSymbol,
      cursiv::function_ref<bool(const ReferenceDescription&)> visitor)
      const override;

  ~DefaultIndexManager() noexcept override = default;

 private:
  // The inverse edge: "what points at this symbol", asked by rename and by
  // find-references, where scanning every document per question would be the
  // whole workspace.
  struct Target {
    DocumentId document;
    NodeId symbol;
    bool operator==(const Target&) const = default;
  };
  struct TargetHash {
    [[nodiscard]] std::size_t operator()(const Target& t) const noexcept;
  };

  static constexpr unsigned kShardBits = 6;
  static constexpr std::size_t kShards = std::size_t{1} << kShardBits;
  // The high bits: a shard places its slots by the low ones.
  [[nodiscard]] static constexpr std::size_t shardOf(
      std::uint64_t hash) noexcept {
    return static_cast<std::size_t>(hash >> (64 - kShardBits));
  }
  struct Hashed {
    std::uint64_t hash;
    const AstNodeDescription* element;
  };

  // Published, not locked: `visitElements` hands candidates to a caller's
  // visitor that reaches back into the index, and a lock held across that
  // callback is a self-deadlock on the first reentrant reader.
  //
  // Split from `Incoming`: collapsed into one snapshot, a reference write
  // would invalidate the exports the linker is reading, paying a rebuild per
  // document.
  struct Exported {
    std::string uri;
    std::vector<AstNodeDescription> elements;
    // The same elements, folded-hashed and grouped by shard on the indexing
    // worker, each group in export order: building a shard hashes nothing.
    std::vector<Hashed> byShard;
    std::array<std::uint32_t, kShards + 1> shardBegin{};
  };
  struct Exports {
    // Owning: a write replaces a row's holder, and a reader still walking this
    // snapshot keeps the row it is on.
    std::vector<std::pair<DocumentId, std::shared_ptr<const Exported>>> rows;
    // Built from the rows, not scanned per lookup: a scan would walk every
    // exported symbol on each cross-document reference.
    //
    // Keyed by the folded name's hash, so a run may also hold another name
    // that collides: a reader filters it, exactly or folded as it asks.
    struct Slot {
      std::uint64_t hash = 0;
      std::uint32_t begin = 0;
      std::uint32_t count = 0;  // zero: free
    };
    // Filled by the first reader that needs it, so the readers of one link
    // build shards side by side instead of queueing behind a single build.
    struct Shard {
      std::atomic<bool> built{false};
      std::mutex building;
      // Open-addressed, power of two, linear probing; each run follows URI
      // order, then the document's export order.
      std::vector<Slot> slots;
      std::vector<const AstNodeDescription*> byName;

      [[nodiscard]] std::size_t slotOf(std::uint64_t hash) const noexcept;
    };
    mutable std::array<Shard, kShards> shards;

    [[nodiscard]] std::span<const AstNodeDescription* const> named(
        std::string_view name) const;
    void fill(Shard& shard, std::size_t at) const;
  };
  struct Incoming {
    std::unordered_map<Target, std::vector<ReferenceDescription>, TargetHash>
        byTarget;
  };
  struct Referenced {
    std::vector<ReferenceDescription> descriptions;
    std::uint64_t targets = 0;
  };

  // The write side. A build spreads each phase across the pool one document at
  // a time, so two workers insert at once and an unguarded insertion is a
  // rehash performed twice. It guards the tables and nothing else: what a
  // document exports and what it points at are computed before it is taken, on
  // the worker's own stack, because those walks are the phase's whole cost.
  mutable std::mutex _mutex;

  // The two halves are separate because two phases fill them, and a change may
  // invalidate one without the other.
  std::unordered_map<DocumentId, std::shared_ptr<Exported>> _exported;
  std::unordered_map<DocumentId, Referenced> _references;
  struct Import {
    std::uint64_t name;
    std::uint32_t position;
  };
  struct Importer {
    DocumentId document;
    std::uint32_t nameIndex;
  };
  // Who looked up which name, cross-indexed so a document's imports leave in
  // constant time each, however many documents share the name.
  std::unordered_map<DocumentId, std::vector<Import>> _importedNames;
  std::unordered_map<std::uint64_t, std::vector<Importer>> _importers;
  // Documents a changed export may have retargeted. A mark survives
  // `dropReferences`: only a successful `updateReferences` or the document's
  // own content drop settles it, or a cancelled relink would be forgotten.
  std::unordered_set<DocumentId> _affected;

  // Null when the matching write dropped it; the first reader after that
  // rebuilds it once under `_mutex` and publishes.
  // Published, not locked: the scope path asks for the export view once per
  // reference, and a `std::atomic<shared_ptr>` would serialise every worker on
  // its control block -- the same convoy a mutex makes, one line down.
  mutable Published<Exports> _exports;
  mutable Published<Incoming> _incoming;

  // `_mutex` held by the caller.
  bool dropContent(DocumentId document);
  bool dropReferences(DocumentId document);
  void affectImporters(const Exported& exports);
  // Each takes `_mutex` only when there is nothing published.
  // The handle keeps the snapshot alive for the whole read, which matters:
  // what these hand back are pointers into it.
  [[nodiscard]] Published<Exports>::Read exportsView() const;
  [[nodiscard]] Published<Incoming>::Read incomingView() const;
};

}  // namespace cursiv
