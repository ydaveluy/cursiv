#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/BlockArena.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/SourceTranslation.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/macros.hpp"

// The concrete syntax tree: a postorder run of 16-byte records, indexed by
// node id. A `const Cst&` changes only the two indexes built on first use.

namespace cursiv {

// Defined at the end of this header: `Cst` returns one, and it needs `Cst`
// complete.
class ReverseNodeRange;
class CstBuilder;

namespace detail {
struct CstWalk;
}
namespace engine {
struct AstBuildCtx;
}
struct Document;

struct Grammar;

// The tree, the text it describes, and the arena both it and the AST come from.
class Cst {
 public:
  // By pointer so `Cst` stays movable: the arena is neither copyable nor
  // movable.
  Cst()
      : _text(&kEmptyDocumentByte, std::size_t{0}),
        _pool(std::make_unique<detail::BlockArena>(kFirstBuffer)) {
    _stream.bind(_pool.get());
  }
  explicit Cst(std::string_view text) : Cst() { set_text(text); }
  // Hand-written so a moved-from tree reports itself empty: `= default` copies
  // the scalars, leaving the source claiming a size over storage it gave away.
  Cst(Cst&& other) noexcept { *this = std::move(other); }
  Cst& operator=(Cst&& other) noexcept {
    if (this == &other) return *this;
    _text = other._text;
    _pool = std::move(other._pool);
    _entryBegin = other._entryBegin;
    _entryEnd = other._entryEnd;
    _entryRoot = other._entryRoot;
    _accepted = other._accepted;
    _diagnostics = std::move(other._diagnostics);
    _stream = std::move(other._stream);
    _enginesTape = other._enginesTape;
    _grammar = other._grammar;
    _owned = other._owned;
    _leaves = std::move(other._leaves);
    _leafIndex = other._leafIndex;
    _nodeRecords = std::move(other._nodeRecords);
    _nodeIndex = other._nodeIndex;
    _translation = std::move(other._translation);
    other.clear();
    return *this;
  }
  ~Cst() = default;

  // Starts another parse, releasing the old tree and any AST on the shared
  // resource. Call `AstArena::release()` first: the arena would allocate its
  // next node from the freed buffers.
  void reset(std::string_view text) {
    // The records live in the resource: unbind the run before the release and
    // bind it again after.
    _stream.bind(nullptr);
    // A moved-from tree has no resource left to release.
    if (_pool != nullptr)
      _pool->release();
    else
      _pool = std::make_unique<detail::BlockArena>(kFirstBuffer);
    _stream.bind(_pool.get());
    clear();
    set_text(text);
  }

 private:
  // The nodes and their list live on the pool the records do: `ParseResult`
  // alone reaches it, so nothing can hold it past a `reset`.
  friend class ParseResult;
  std::pmr::memory_resource* memory() const { return _pool.get(); }

  // The builder stores joined text here; others go through the document.
  friend struct engine::AstBuildCtx;
  friend struct Document;
  // Joins `parts` into text that lives as long as the tree. A null view means
  // an unwritten member, even at length zero.
  std::string_view keep_text(std::span<const std::string_view> parts) const {
    std::size_t size = 0;
    for (const std::string_view part : parts) {
      if (part.size() > std::numeric_limits<std::size_t>::max() - size)
        throw std::bad_alloc();
      size += part.size();
    }
    auto* const out = static_cast<char*>(
        _pool->allocate(size == 0 ? 1 : size, alignof(char)));
    char* at = out;
    for (const std::string_view part : parts) {
      if (part.empty()) continue;
      std::char_traits<char>::copy(at, part.data(), part.size());
      at += part.size();
    }
    return std::string_view(out, size);
  }

  // Resets every field but the resource. Not `*this = Cst{}`, which would
  // allocate a pool.
  void clear() noexcept {
    _text = std::string_view(&kEmptyDocumentByte, std::size_t{0});
    _entryBegin = 0;
    _entryEnd = 0;
    _entryRoot = kNoNode;
    _accepted = false;
    _diagnostics.clear();
    _stream.clear();
    _enginesTape = false;
    _grammar = nullptr;
    _owned = {};
    _leaves.clear();
    _leafIndex = 0;
    _nodeRecords.clear();
    _nodeIndex = 0;
    _translation.reset();
  }

  void set_text(std::string_view text) {
    // Checked once: every offset in the tree is 32 bits, so a larger document
    // would report wrong spans rather than an error.
    if (text.size() > kMaxDocument)
      throw std::overflow_error(
          "cursiv: document exceeds the size limit, just under 4 GiB");
    // An empty view may carry a null pointer; one stable address keeps offset
    // arithmetic defined.
    _text = text.empty() ? std::string_view(&kEmptyDocumentByte, std::size_t{0})
                         : text;
  }

 public:
  std::string_view text() const { return _text; }
  NodeCount size() const { return _stream.records(); }

  // The entry is explicit because hidden trivia can be roots of the forest, so
  // neither zero nor `size()-1` finds it. `has_entry()` means the entry rule
  // completed; a parse may still reject text after it and keep the root.
  bool has_entry() const { return _entryRoot != kNoNode; }
  bool accepted() const { return _accepted; }
  // Whether a repair happened, read from the diagnostics: `accepted() &&
  // !recovered()` is a clean parse, `accepted() && recovered()` a repaired one.
  bool recovered() const {
    return std::ranges::any_of(_diagnostics, &ParseDiagnostic::isSyntax);
  }
  // What each repair did, in the order the repairs were made -- a repair that
  // gives up an open construct names the span it abandoned, which can start
  // before the one reported just before it. Empty on a clean parse.
  std::span<const ParseDiagnostic> diagnostics() const {
    return {_diagnostics.data(), _diagnostics.size()};
  }
  NodeId entry_root() const { return _entryRoot; }
  NodeId entry_begin() const { return _entryBegin; }
  NodeId entry_end() const { return _entryEnd; }

  const CstNode& operator[](NodeId id) const { return node(id); }

  std::string_view text(NodeId id) const noexcept { return text(node(id)); }
  std::string_view text(const CstNode& n) const noexcept {
    return std::string_view(_text.data() + n.begin, n.end - n.begin);
  }
  // The text the parser read for `id`: `text(id)`, or in a translated
  // document the translated text the node was parsed from.
  std::string_view spelling(NodeId id) const noexcept {
    const CstNode& n = node(id);
    if (_translation == nullptr) return text(n);
    const TextRange at = _translation->text_of(id, {n.begin, n.end});
    return _translation->text().substr(at.begin, at.end - at.begin);
  }

  // Postorder gives the last child in constant time, not the next sibling:
  // walk `[subtree_begin(id), id]`, or `reverse_children` reversed.
  NodeId subtree_begin(NodeId id) const {
    const NodeId extent = node(id).subtreeSize;
    return id - (extent - 1);
  }
  bool childless(NodeId id) const { return node(id).subtreeSize == 1; }
  bool contains(NodeId ancestor, NodeId descendant) const {
    return descendant >= subtree_begin(ancestor) && descendant <= ancestor;
  }

  ReverseNodeRange reverse_children(NodeId parent) const;

  // Whether an engine wrote the records. Hand-built records carry no engine
  // data: check this before reading it.
  [[nodiscard]] bool engine_stamped() const { return _enginesTape; }
  // The grammar of the engine's records; null when no language parser
  // finished the tree.
  [[nodiscard]] const Grammar* grammar() const { return _grammar; }
  [[nodiscard]] OwnedNodes owned() const { return _owned; }
  // Null unless the language's translator rewrote the document: the AST's
  // views point into its text, while every offset here is the document's.
  [[nodiscard]] const SourceTranslation* translation() const {
    return _translation.get();
  }

  // The leaves in document order, without the index: a linear consumer pays
  // nothing for a record it does not read. `f` takes a `const CstLeaf&`.
  template <class F>
  void visit_leaves(F&& f) const {
    for (NodeId id = 0; id < size(); ++id) {
      const CstNode& n = node(id);
      if (n.subtreeSize == 1) f(leaf_of(id, n));
    }
  }

  // The leaves by rank, which is document order: ends never decrease, so a
  // caret is one binary search away. The index is built once, on the first
  // question -- a service that never asks a caret never pays for it.

  [[nodiscard]] CstLeaf leaf(NodeCount rank) const {
    ensure_leaf_index();
    const NodeId id = _leaves[rank];
    return leaf_of(id, node(id));
  }
  // What bounds a rank: `leaves_before` answers one past the last leaf behind a
  // caret, which is a valid rank only against this.
  [[nodiscard]] NodeCount leaf_count() const {
    ensure_leaf_index();
    return static_cast<NodeCount>(_leaves.size());
  }

  // How many leaves end at or before `offset` -- the rank one past the last
  // leaf a caret sits behind.
  [[nodiscard]] NodeCount leaves_before(TextOffset offset) const {
    ensure_leaf_index();
    NodeCount lo = 0, hi = static_cast<NodeCount>(_leaves.size());
    while (lo < hi) {
      const NodeCount mid = std::midpoint(lo, hi);
      if (node(_leaves[mid]).end <= offset)
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }

 private:
  // The only writer: an engine finishes the tree through the builder.
  friend class CstBuilder;

  // Marks the records as written by an engine.
  void bind_stream() {
    _leaves.clear();
    _enginesTape = true;
    _leafIndex = 0;
    _nodeRecords.clear();
    _nodeIndex = 0;
  }

  void bind_entry(NodeId begin, NodeId end, NodeId root) {
    _entryBegin = begin;
    _entryEnd = end;
    _entryRoot = root;
  }
  void bind_accepted(bool accepted) { _accepted = accepted; }
  // Set once a build is over: `owned` must not grow after this. `addressof`,
  // not `&`: the grammar DSL overloads the operator, and `Grammar` is only
  // declared here.
  void bind_model(const Grammar& grammar, OwnedNodes owned) {
    _grammar = std::addressof(grammar);
    _owned = owned;
    _nodeRecords.clear();
    _nodeIndex = 0;
  }
  // Moves a parse of the translated text to the document's offsets, and
  // keeps the text its AST points into. The text is bound first: a node's
  // destructor may read it.
  void bind_translation(std::string_view document,
                        std::unique_ptr<SourceTranslation> translation) {
    SourceTranslation& bound = *translation;
    _translation = std::move(translation);
    set_text(document);
    bound.bind(
        size() != 0 ? std::span(&_stream.at(0), size()) : std::span<CstNode>{},
        std::span(_diagnostics));
  }

  const CstNode& node(NodeId id) const { return _stream.at(id); }

  static CstLeaf leaf_of(NodeId id, const CstNode& n) {
    return CstLeaf{id, n.begin, n.end, n.hidden != 0, n.recovered != 0};
  }

  void ensure_leaf_index() const {
    if (!published(_leafIndex)) [[unlikely]]
      index_leaves();
  }
  // Builds the leaf index once.
  CURSIV_NOINLINE void index_leaves() const {
    build_once(_leafIndex, [this] {
      // Room for every record, the room past the leaves never written; Wasm
      // memory charges for that room, so there the leaves are counted first.
      std::size_t room = size();
      if constexpr (sizeof(void*) < 8) {
        room = 0;
        for (NodeId id = 0; id < size(); ++id)
          room += node(id).subtreeSize == 1 ? 1 : 0;
      }
      _leaves.clear();
      _leaves.reserve(room);
      for (NodeId id = 0; id < size(); ++id)
        if (node(id).subtreeSize == 1) _leaves.push_back(id);
    });
  }

  // The walks of `CstUtils`, which ask `carries_node` of every record they
  // pass: a construction's search must stop at a nested node's record.
  friend struct detail::CstWalk;

  // Whether a node of `owned()` was built from the record: one bit a record,
  // so a walk asks it at no search.
  [[nodiscard]] bool carries_node(NodeId id) const {
    if (!published(_nodeIndex)) [[unlikely]]
      index_nodes();
    return ((_nodeRecords[id / 64] >> (id % 64)) & 1U) != 0;
  }
  // Defined in `CstUtils.cpp`, where `AstNode` is complete and the walks that
  // read it live.
  CURSIV_NOINLINE void index_nodes() const;

  // Builds an index once and publishes it. One lock for every index, so a
  // second reader waits instead of building it too. `build` clears the list.
  template <class Build>
  void build_once(std::uint8_t& flag, Build&& build) const {
    const std::lock_guard lock(_indexLock);
    if (!published(flag)) {
      build();
      publish(flag);
    }
  }

  // Whether the index a reader depends on is already visible to it. A plain
  // byte spun through `atomic_ref` and not a `std::atomic` member, which the
  // tree's move constructor could not carry.
  static bool published(std::uint8_t& flag) noexcept {
    return std::atomic_ref<std::uint8_t>(flag).load(
               std::memory_order_acquire) != 0;
  }
  static void publish(std::uint8_t& flag) noexcept {
    std::atomic_ref<std::uint8_t>(flag).store(1, std::memory_order_release);
  }

  static constexpr std::size_t kFirstBuffer =
      static_cast<std::size_t>(64 * 1024);
  static constexpr char kEmptyDocumentByte = '\0';

  std::string_view _text;
  std::unique_ptr<detail::BlockArena> _pool;
  NodeId _entryBegin = 0;
  NodeId _entryEnd = 0;
  NodeId _entryRoot = kNoNode;
  bool _accepted = false;
  std::vector<ParseDiagnostic> _diagnostics;
  // The records the engine appended, or the ones a caller built by hand
  // through `CstBuilder`.
  CstRun _stream;
  // Whether an engine wrote the records, rather than `CstBuilder`.
  bool _enginesTape = false;
  const Grammar* _grammar = nullptr;
  OwnedNodes _owned;
  // Built on the first caret question and never after: a rank in it is
  // document order, so a caret is one bisection away.
  mutable std::vector<NodeId> _leaves;
  // Whether the leaf index a reader depends on is already visible to it.
  mutable std::uint8_t _leafIndex = 0;
  // Built on the first walk of `CstUtils` and never after: bit `id` says a
  // node of `_owned` was built from record `id`.
  mutable std::vector<std::uint64_t> _nodeRecords;
  mutable std::uint8_t _nodeIndex = 0;
  // Held only while an index is built. Neither moved nor reset.
  mutable std::mutex _indexLock;
  // The translated text the AST points into, and its map to the document.
  std::unique_ptr<const SourceTranslation> _translation;
};

// Reverse view over a forest or one parent's children. `_cursor` is the
// exclusive end of the unvisited prefix; incrementing skips a whole subtree.
class ReverseNodeRange {
 public:
  class iterator {
   public:
    using iterator_category =
        std::input_iterator_tag;  // operator* is a prvalue
    using value_type = NodeId;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = NodeId;
    using iterator_concept = std::forward_iterator_tag;

    iterator() = default;

    NodeId operator*() const { return _cursor - 1; }
    iterator& operator++() {
      _cursor = _cst->subtree_begin(_cursor - 1);
      return *this;
    }
    iterator operator++(int) {
      iterator copy = *this;
      ++*this;
      return copy;
    }
    bool operator==(const iterator&) const = default;

   private:
    friend class ReverseNodeRange;
    iterator(const Cst* cst, NodeId cursor) : _cst(cst), _cursor(cursor) {}
    const Cst* _cst = nullptr;
    NodeId _cursor = 0;
  };

  iterator begin() const { return iterator(_cst, _firstCursor); }
  iterator end() const { return iterator(_cst, _lastCursor); }

 private:
  friend class Cst;
  ReverseNodeRange(const Cst* cst, NodeId firstCursor, NodeId lastCursor)
      : _cst(cst), _firstCursor(firstCursor), _lastCursor(lastCursor) {}
  const Cst* _cst;
  NodeId _firstCursor;
  NodeId _lastCursor;
};

inline ReverseNodeRange Cst::reverse_children(NodeId parent) const {
  return ReverseNodeRange(this, parent, subtree_begin(parent));
}

}  // namespace cursiv
