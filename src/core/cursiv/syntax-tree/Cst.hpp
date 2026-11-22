#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
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
// node id. A `const Cst&` changes only the index built on first use.

namespace cursiv {

// Defined at the end of this header: `Cst` returns one, and it needs `Cst`
// complete.
class ReverseNodeRange;

class Cst;
namespace detail {
template <bool, class Match, class Take>
void walk_cst(const Cst&, NodeId, const Match&, const Take&);
}
class CstBuilder;

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
  explicit Cst(std::string_view text) : Cst() { setText(text); }
  // Hand-written so a moved-from tree reports itself empty: `= default` copies
  // the scalars, leaving the source claiming a size over storage it gave away.
  Cst(Cst&& other) noexcept { *this = std::move(other); }
  Cst& operator=(Cst&& other) noexcept {
    if (this == &other) return *this;
    _text = other._text;
    _pool = std::move(other._pool);
    _entryRoot = other._entryRoot;
    _accepted = other._accepted;
    _diagnostics = std::move(other._diagnostics);
    _stream = std::move(other._stream);
    _grammar = other._grammar;
    _owned = other._owned;
    _nodeRecords = std::move(other._nodeRecords);
    _nodeIndex = other._nodeIndex;
    _translation = std::move(other._translation);
    other.clear();
    return *this;
  }
  ~Cst() = default;

  // Starts another parse and releases the shared resource. Existing AST
  // nodes must have been destroyed first.
  void reset(std::string_view text) {
    // A moved-from tree has no resource left to release.
    if (_pool != nullptr)
      _pool->release();
    else
      _pool = std::make_unique<detail::BlockArena>(kFirstBuffer);
    _stream.bind(_pool.get());
    clear();
    setText(text);
  }

 private:
  // The nodes and their list live on the pool the records do: `ParseResult`
  // alone reaches it, so nothing can hold it past a `reset`.
  friend class ParseResult;

  // Resets every field but the resource. Not `*this = Cst{}`, which would
  // allocate a pool.
  void clear() noexcept {
    _text = std::string_view(&kEmptyDocumentByte, std::size_t{0});
    _entryRoot = kNoNode;
    _accepted = false;
    _diagnostics.clear();
    _stream.clear();
    _grammar = nullptr;
    _owned = {};
    _nodeRecords.clear();
    _nodeIndex = 0;
    _translation.reset();
  }

  void setText(std::string_view text) {
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
  // neither zero nor `size()-1` finds it. `hasEntry()` means the entry rule
  // completed; a parse may still reject text after it and keep the root.
  bool hasEntry() const { return _entryRoot != kNoNode; }
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
  NodeId entryRoot() const { return _entryRoot; }
  NodeId entryBegin() const { return hasEntry() ? subtreeBegin(_entryRoot) : 0; }
  NodeId entryEnd() const { return hasEntry() ? _entryRoot + 1 : 0; }

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
    const TextRange at = _translation->textOf(id, {n.begin, n.end});
    return _translation->text().substr(at.begin, at.end - at.begin);
  }

  // Postorder gives the last child in constant time, not the next sibling:
  // walk `[subtreeBegin(id), id]`, or `reverseChildren` reversed.
  NodeId subtreeBegin(NodeId id) const {
    const NodeId extent = node(id).subtreeSize;
    return id - (extent - 1);
  }
  bool childless(NodeId id) const { return node(id).subtreeSize == 1; }
  bool contains(NodeId ancestor, NodeId descendant) const {
    return descendant >= subtreeBegin(ancestor) && descendant <= ancestor;
  }

  ReverseNodeRange reverseChildren(NodeId parent) const;

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
  void visitLeaves(F&& f) const {
    for (NodeId id = 0; id < size(); ++id) {
      const CstNode& n = node(id);
      if (n.subtreeSize == 1) f(leafOf(id, n));
    }
  }

  // The leaves are found from the records: ends never decrease over them, so a
  // caret is one bisection away, and record 0 is a leaf. A walk steps from one
  // leaf to the next over the parents that close between them.

  // Record `id`, which has no child, as a leaf.
  [[nodiscard]] CstLeaf asLeaf(NodeId id) const { return leafOf(id, node(id)); }

  // The first leaf ending past `offset`, or `size()`. A parent that read
  // blanks past its last leaf can be the first record ending past it; its
  // leaves then all end before, and the answer is the next leaf.
  [[nodiscard]] NodeId leafAfter(TextOffset offset) const {
    NodeId lo = 0, hi = size();
    while (lo < hi) {
      const NodeId mid = std::midpoint(lo, hi);
      if (node(mid).end <= offset)
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo == size() || childless(lo) ? lo : nextLeaf(lo);
  }

  // The last leaf before record `at`, which is above 0; `at` may be `size()`.
  [[nodiscard]] NodeId leafBefore(NodeId at) const {
    do --at;
    while (!childless(at));
    return at;
  }

  // The first leaf after record `id`, or `size()`.
  [[nodiscard]] NodeId nextLeaf(NodeId id) const {
    do ++id;
    while (id < size() && !childless(id));
    return id;
  }

 private:
  // The only writer: an engine finishes the tree through the builder.
  friend class CstBuilder;


  void bindEntry(NodeId root) {
    // What `leafAfter` bisects on, checked once a parse is over.
    assert(std::ranges::is_sorted(size() != 0
                                      ? std::span(&_stream.at(0), size())
                                      : std::span<const CstNode>{},
                                  {}, &CstNode::end) &&
           "record ends never decrease");
    _entryRoot = root;
  }
  void bindAccepted(bool accepted) { _accepted = accepted; }
  // Set once a build is over: `owned` must not grow after this. `addressof`,
  // not `&`: the grammar DSL overloads the operator, and `Grammar` is only
  // declared here.
  void bindModel(const Grammar& grammar, OwnedNodes owned) {
    _grammar = std::addressof(grammar);
    _owned = owned;
    _nodeRecords.clear();
    _nodeIndex = 0;
  }
  // Moves a parse of the translated text to the document's offsets, and
  // keeps the text its AST points into. The text is bound first: a node's
  // destructor may read it.
  void bindTranslation(std::string_view document,
                       std::unique_ptr<SourceTranslation> translation) {
    SourceTranslation& bound = *translation;
    _translation = std::move(translation);
    setText(document);
    bound.bind(
        size() != 0 ? std::span(&_stream.at(0), size()) : std::span<CstNode>{},
        std::span(_diagnostics));
  }

  const CstNode& node(NodeId id) const { return _stream.at(id); }

  static CstLeaf leafOf(NodeId id, const CstNode& n) {
    return CstLeaf{id, n.begin, n.end, n.isHidden(), n.isRecovered()};
  }

  template <bool, class Match, class Take>
  friend void detail::walk_cst(const Cst&, NodeId, const Match&, const Take&);

  // The records a node of `owned()` was built from, a bit a record: a walk
  // asks once, then reads the bits at no search.
  [[nodiscard]] const std::uint64_t* nodeRecords() const {
    if (!published(_nodeIndex)) [[unlikely]]
      indexNodes();
    return _nodeRecords.data();
  }
  // Builds that index once and publishes it. Defined in `CstUtils.cpp`, where
  // `AstNode` is complete and the walks that read it live.
  CURSIV_NOINLINE void indexNodes() const;

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
  NodeId _entryRoot = kNoNode;
  bool _accepted = false;
  std::vector<ParseDiagnostic> _diagnostics;
  // The records the engine appended, or the ones a caller built by hand
  // through `CstBuilder`.
  CstRun _stream;
  const Grammar* _grammar = nullptr;
  OwnedNodes _owned;
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
  class Iterator {
   public:
    using iterator_category =
        std::input_iterator_tag;  // operator* is a prvalue
    using value_type = NodeId;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = NodeId;
    using iterator_concept = std::forward_iterator_tag;

    Iterator() = default;

    NodeId operator*() const { return _cursor - 1; }
    Iterator& operator++() {
      _cursor = _cst->subtreeBegin(_cursor - 1);
      return *this;
    }
    Iterator operator++(int) {
      Iterator copy = *this;
      ++*this;
      return copy;
    }
    bool operator==(const Iterator&) const = default;

   private:
    friend class ReverseNodeRange;
    Iterator(const Cst* cst, NodeId cursor) : _cst(cst), _cursor(cursor) {}
    const Cst* _cst = nullptr;
    NodeId _cursor = 0;
  };

  Iterator begin() const { return Iterator(_cst, _firstCursor); }
  Iterator end() const { return Iterator(_cst, _lastCursor); }

 private:
  friend ReverseNodeRange Cst::reverseChildren(NodeId) const;
  ReverseNodeRange(const Cst* cst, NodeId firstCursor, NodeId lastCursor)
      : _cst(cst), _firstCursor(firstCursor), _lastCursor(lastCursor) {}
  const Cst* _cst;
  NodeId _firstCursor;
  NodeId _lastCursor;
};

inline ReverseNodeRange Cst::reverseChildren(NodeId parent) const {
  return ReverseNodeRange(this, parent, subtreeBegin(parent));
}

}  // namespace cursiv
