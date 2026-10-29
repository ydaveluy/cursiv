#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/core/syntax-tree/OwnedNodes.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/SourceTranslation.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"

// The concrete syntax tree: a postorder run of 16-byte records, out of a
// std::pmr resource shared with the AST, read at the index that is its node
// id. The one mutation a `const Cst&` performs is the leaf index a caret
// question lazily builds; every other read is pure.

namespace cursiv {

// Defined at the foot of this header: `Cst` returns one, and it needs `Cst`
// complete to walk. A client that has the tree can therefore iterate it --
// which a separate header could not promise.
class ReverseNodeRange;
class CstBuilder;

struct Grammar;

// The tree, the text it describes, and the arena both it and the AST come from.
class Cst {
 public:
  // By pointer so `Cst` stays movable: a monotonic buffer resource is neither
  // copyable nor movable.
  Cst()
      : _text(&kEmptyDocumentByte, std::size_t{0}),
        _pool(std::make_unique<std::pmr::monotonic_buffer_resource>(
            kFirstBuffer)) {
    _stream.bind(_pool.get());
  }
  explicit Cst(std::string_view text) : Cst() { set_text(text); }
  // Hand-written so a moved-from tree reports itself empty: `= default` copies
  // the scalars, leaving the source claiming a size over storage it no longer
  // owns.
  Cst(Cst&& other) noexcept
      : _text(other._text),
        _pool(std::move(other._pool)),
        _entryBegin(other._entryBegin),
        _entryEnd(other._entryEnd),
        _entryRoot(other._entryRoot),
        _accepted(other._accepted),
        _diagnostics(std::move(other._diagnostics)),
        _stream(std::move(other._stream)),
        _enginesTape(other._enginesTape),
        _grammar(other._grammar),
        _owned(other._owned),
        _leaves(std::move(other._leaves)),
        _leafIndex(other._leafIndex),
        _translation(std::move(other._translation)) {
    other.clear();
  }
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
    _translation = std::move(other._translation);
    other.clear();
    return *this;
  }
  ~Cst() = default;

  // Start another parse here, releasing the old tree and any AST built from the
  // shared resource. Use-after-free hazard: an `AstArena` on `memory()` keeps a
  // chunk of the buffers this frees and would carve its next node there, so
  // `AstArena::release()` comes first.
  void reset(std::string_view text) {
    // The records live in the resource, so the run lets go before the release
    // and takes the new one after.
    _stream.bind(nullptr);
    // A moved-from tree has no resource left to release.
    if (_pool != nullptr)
      _pool->release();
    else
      _pool =
          std::make_unique<std::pmr::monotonic_buffer_resource>(kFirstBuffer);
    _stream.bind(_pool.get());
    clear();
    set_text(text);
  }

 private:
  // Every field but the resource, back to a fresh tree's. Not `*this =
  // Cst{}`, which would allocate a pool for a dying object.
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
    _translation.reset();
  }

  void set_text(std::string_view text) {
    // Checked once: every offset in the tree is 32 bits, so a larger document
    // would report wrong spans rather than an error.
    if (text.size() > kMaxDocument)
      throw std::overflow_error("cursiv: document exceeds 4 GiB");
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
  // A repair happened, which is the diagnostics saying so and no second
  // place to keep it in step: `accepted() && !recovered()` is the clean
  // parse, `accepted() && recovered()` the repaired one.
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

  // Const tree, mutable resource: `ParseResult::cst()` hands out a `const
  // Cst&` to make the use-after-free above unspellable, and the builder must
  // still be told where the AST allocates.
  std::pmr::memory_resource* memory() const { return _pool.get(); }

  const CstNode& operator[](NodeId id) const { return node(id); }

  std::string_view text(NodeId id) const noexcept {
    const CstNode& n = node(id);
    return std::string_view(_text.data() + n.begin, n.end - n.begin);
  }

  // Postorder makes the last child constant-time; a forward sibling cannot be
  // computed from one subtree size. Source order is the caller's job:
  // `[subtree_begin(id), id]` over a subtree, `reverse_children` reversed over
  // one node's children.
  NodeId subtree_begin(NodeId id) const {
    const NodeId extent = node(id).subtreeSize;
    return id - (extent - 1);
  }
  bool childless(NodeId id) const { return node(id).subtreeSize == 1; }
  bool contains(NodeId ancestor, NodeId descendant) const {
    return descendant >= subtree_begin(ancestor) && descendant <= ancestor;
  }

  ReverseNodeRange reverse_children(NodeId parent) const;

  // Whether an engine wrote this tape. A hand-built one carries no stamp --
  // every record reads zero, which an engine decoding it would take for a
  // meaning of its own. A reader that turns the stamp into an answer asks
  // first.
  [[nodiscard]] bool engine_stamped() const { return _enginesTape; }
  // The tree binds the grammar and AST ownership for every stamped record.
  // Null for a tape no language parser finished.
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
      const NodeCount mid = lo + (hi - lo) / 2;
      if (node(_leaves[mid]).end <= offset)
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }

 private:
  // One writer: an engine states what a finished pass knows through the
  // builder, and nothing else reaches the run's lifecycle.
  friend class CstBuilder;

  // The engine hands the run over, and states that the stamps are its own.
  void bind_stream() {
    _leaves.clear();
    _enginesTape = true;
    _leafIndex = 0;
  }

  void bind_entry(NodeId begin, NodeId end, NodeId root) {
    _entryBegin = begin;
    _entryEnd = end;
    _entryRoot = root;
  }
  void bind_accepted(bool accepted) { _accepted = accepted; }
  // Stated once a build is over: `owned` must not grow under this.
  // `addressof`, not `&`: the grammar DSL overloads the operator, and here
  // the type is only declared, which the overload's concept cannot answer.
  void bind_model(const Grammar& grammar, OwnedNodes owned) {
    _grammar = std::addressof(grammar);
    _owned = owned;
  }
  // A parse of the translated text moves to the document's offsets and
  // keeps the text its AST borrows.
  void bind_translation(std::string_view document,
                        std::unique_ptr<const SourceTranslation> translation) {
    set_text(document);
    if (size() != 0) translation->to_document({&_stream.at(0), size()});
    translation->to_document(std::span(_diagnostics));
    _translation = std::move(translation);
  }

  // One storage, and this is the read of it: the records the parser wrote are
  // the records every reader reads, at the index that is the node id.
  const CstNode& node(NodeId id) const { return _stream.at(id); }

  static CstLeaf leaf_of(NodeId id, const CstNode& n) {
    return CstLeaf{id, n.begin, n.end, n.hidden != 0, n.recovered != 0};
  }

  void ensure_leaf_index() const {
    if (!published(_leafIndex)) [[unlikely]]
      index_leaves();
  }
  // The index a second caret question finds ready.
  CURSIV_NOINLINE void index_leaves() const {
    // Released from a destructor: `build_leaf_index` allocates, and a byte held
    // past a throw blocks every later caret question.
    struct Held {
      std::atomic_ref<std::uint8_t> lock;
      ~Held() {
        lock.store(0, std::memory_order_release);
        lock.notify_all();
      }
    };
    std::atomic_ref<std::uint8_t> lock(_leafLock);
    while (lock.exchange(1, std::memory_order_acquire) != 0)
      lock.wait(1, std::memory_order_relaxed);
    const Held held{lock};
    if (!published(_leafIndex)) {
      build_leaf_index();
      publish(_leafIndex);
    }
  }
  CURSIV_NOINLINE void build_leaf_index() const {
    // Cleared, not appended to: a build cut short leaves the flag unpublished
    // and this list half-written.
    _leaves.clear();
    _leaves.reserve(size());
    for (NodeId id = 0; id < size(); ++id)
      if (node(id).subtreeSize == 1) _leaves.push_back(id);
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
  std::unique_ptr<std::pmr::monotonic_buffer_resource> _pool;
  NodeId _entryBegin = 0;
  NodeId _entryEnd = 0;
  NodeId _entryRoot = kNoNode;
  bool _accepted = false;
  std::vector<ParseDiagnostic> _diagnostics;
  // The tree, and the only copy of it: the records the engine appended, or
  // the ones a caller built by hand through `CstBuilder`.
  CstRun _stream;
  // Whether the records carry an engine's stamp, or a hand builder's zeros.
  bool _enginesTape = false;
  const Grammar* _grammar = nullptr;
  OwnedNodes _owned;
  // Built on the first caret question and never after: a rank in it is
  // document order, so a caret is one bisection away.
  mutable std::vector<NodeId> _leaves;
  // Whether the leaf index a reader depends on is already visible to it.
  mutable std::uint8_t _leafIndex = 0;
  // Held only while the leaf index is built, so a second reader waits instead
  // of building it beside the first. Neither moved nor reset.
  mutable std::uint8_t _leafLock = 0;
  // The translated text the AST borrows, and its map to the document.
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
    bool operator==(const iterator& other) const {
      return _cst == other._cst && _cursor == other._cursor;
    }

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
