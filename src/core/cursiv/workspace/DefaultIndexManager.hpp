#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cursiv/services/DefaultSharedCoreService.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/Published.hpp"
#include "cursiv/workspace/IndexManager.hpp"

// One row per document, so removing one is dropping its row: an index keyed the
// other way would have to scan everything a document ever exported.

namespace cursiv {

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
  // What points at a symbol, for rename and find-references.
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

  // Published, not locked: a visitor may reach back into the index. Apart from
  // `Incoming`, so a reference write does not drop the exports the linker
  // reads.
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
    // Built from the rows so a lookup does not scan every export. Keyed by the
    // folded name's hash: a run may hold a colliding name, which a reader
    // filters.
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
  // What `visitReferences` walks: the published view, or a reader's own when a
  // write changed the rows it was built from.
  class IncomingView {
   public:
    explicit IncomingView(Published<Incoming>::Read published) noexcept
        : _published(std::move(published)) {}
    explicit IncomingView(std::shared_ptr<const Incoming> own) noexcept
        : _own(std::move(own)) {}
    [[nodiscard]] const Incoming* operator->() const noexcept {
      return _own != nullptr ? _own.get() : _published->get();
    }

   private:
    std::optional<Published<Incoming>::Read> _published;
    std::shared_ptr<const Incoming> _own;
  };

  // The write side, guarding the tables only: two workers of a phase insert at
  // once. Exports and references are computed before it is taken.
  mutable std::mutex _mutex;

  // The two halves are separate because two phases fill them, and a change may
  // invalidate one without the other.
  std::unordered_map<DocumentId, std::shared_ptr<Exported>> _exported;
  // Shared, so a reader groups and sorts them after releasing the lock.
  std::unordered_map<DocumentId, std::shared_ptr<const Referenced>> _references;
  // Counts the releases of `_incoming`, under `_mutex`: a view is published
  // only if no release happened since its rows were read.
  std::uint64_t _referenceWrites = 0;
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
  // own content drop clears it, or a cancelled relink would be forgotten.
  std::unordered_set<DocumentId> _affected;

  // Null once a write drops it, before changing its table; the next reader
  // rebuilds and publishes it, unless a write came in since. Published rather
  // than atomic: the scope path reads the export view once per reference.
  mutable Published<Exports> _exports;
  mutable Published<Incoming> _incoming;

  // `_mutex` held by the caller.
  bool dropContent(DocumentId document);
  bool dropReferences(DocumentId document);
  void clearIncoming();
  void affectImporters(const Exported& exports);
  // Each takes `_mutex` only when nothing is published. The handle keeps the
  // snapshot alive for the whole read, as the answers point into it.
  [[nodiscard]] Published<Exports>::Read exportsView() const;
  [[nodiscard]] IncomingView incomingView() const;
};

}  // namespace cursiv
