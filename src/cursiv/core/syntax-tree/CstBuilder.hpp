// How a tree is produced, and the only writer `Cst` admits: enter/exit,
// leaf/hidden, and the binds a finished pass states its result through. An
// engine reaches the run directly through `stream()`; anyone else drives the
// calls above.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/core/syntax-tree/OwnedNodes.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"

namespace cursiv {

struct Grammar;

class CstBuilder {
 public:
  // A strong four-byte token: a count cannot be forged, and a node id cannot
  // be passed to `exit`.
  class Open {
    friend class CstBuilder;
    explicit Open(NodeCount count) : _count(count) {}
    NodeCount _count;
  };

  explicit CstBuilder(Cst& cst) : _cst(cst) {}

  // Opening performs no write and needs no matching cancellation on failure.
  [[nodiscard]] Open enter() const noexcept { return Open(_cst.size()); }

  // The run's law: ends never decrease, hidden records included. A parent
  // therefore goes right after its last visible descendant -- before any
  // trailing trivia -- and its end is that descendant's.
  NodeId exit(Open from, TextOffset begin, std::uint32_t engine) {
    assert(from._count <= _cst.size() &&
           "a CST parent cannot start after its children");
    const NodeId id = lastVisibleNodeId + 1;
    if (id <= from._count) [[unlikely]]
      return exit_empty(from, begin, engine);
    // Recovery may insert a child before the token at which its parent was
    // opened. The parent still covers that child, including an insertion
    // before ignored trailing whitespace at the end of a partial document.
    const TextOffset first = _cst._stream.at(from._count).begin;
    if (begin > first) begin = first;
    const TextOffset end = _cst._stream.at(lastVisibleNodeId).end;
    const NodeCount extent = id - from._count + 1;
    if (id == _cst.size()) [[likely]]
      _cst._stream.close(begin, end, extent, engine);
    else
      _cst._stream.insert(id, begin, end, extent, engine);
    lastVisibleNodeId = id;
    return id;
  }

  // Inlined like the run's own writers: an engine lays one per token.
  CURSIV_ALWAYS_INLINE NodeId leaf(TextOffset begin, TextOffset end,
                                   std::uint32_t engine) {
    _cst._stream.leaf(begin, end, engine);
    return lastVisibleNodeId = _cst.size() - 1;
  }
  CURSIV_ALWAYS_INLINE NodeId hidden(TextOffset begin, TextOffset end,
                                     std::uint32_t engine) {
    _cst._stream.hidden(begin, end, engine);
    return _cst.size() - 1;
  }

  // A repair, as a value -- appended by the surviving attempt, since what a
  // rewind dropped never reaches here. This list is what `Cst::recovered()`
  // reads: a repair and its record cannot drift apart if there is only one
  // of them.
  void note_repair(DiagnosticKind kind, ElementId element, TextOffset begin,
                   TextOffset end, std::vector<ElementId> also = {}) {
    // Twice the same sentence is one fault seen twice: nested constructs
    // closing at one point owe the same closer.
    if (!_cst._diagnostics.empty()) {
      const ParseDiagnostic& last = _cst._diagnostics.back();
      if (last.kind == kind && last.element == element && last.begin == begin &&
          last.end == end)
        return;
    }
    _cst._diagnostics.push_back(
        {kind, element, std::move(also), begin, end, {}});
  }

  // Where an engine writes its records, and where every reader reads them:
  // one run, indexed by node id.
  CstRun& stream() { return _cst._stream; }
  const CstRun& stream() const { return _cst._stream; }
  // States that the records on the run are an engine's, stamps included.
  void bind_stream() { _cst.bind_stream(); }
  void reserve_stream(std::size_t words) { _cst._stream.reserve_for(words); }

  // The two facts a completed pass states about its run. Guarded: a caller may
  // name a root no record answers to; such a root binds none, and an AST build
  // answers `no_entry` rather than indexing past the end.
  void bind_entry(NodeId begin, NodeId end, NodeId root) noexcept {
    const bool in_range = root == kNoNode || root < _cst.size();
    _cst.bind_entry(begin, end, in_range ? root : kNoNode);
  }
  void bind_accepted(bool accepted) noexcept { _cst.bind_accepted(accepted); }
  void bind_model(const Grammar& grammar, OwnedNodes owned) noexcept {
    _cst.bind_model(grammar, owned);
  }
  void bind_translation(std::string_view document,
                        std::unique_ptr<const SourceTranslation> translation) {
    _cst.bind_translation(document, std::move(translation));
  }

 private:
  // The empty composite goes at its region's open, not at the run's end:
  // records before the open end at or before its `begin`, trivia taken
  // inside the region ends after it -- appending past that trivia would
  // break the run's end order. It is the last visible record then: the
  // enclosing exit must land after it, or the subtree would lose a child.
  NodeId exit_empty(Open from, TextOffset begin, std::uint32_t engine) {
    const NodeId id = from._count;
    if (id == _cst.size())
      _cst._stream.leaf(begin, begin, engine);
    else
      _cst._stream.insert(id, begin, begin, 1, engine);
    lastVisibleNodeId = id;
    return id;
  }

  Cst& _cst;
  NodeId lastVisibleNodeId = kNoNode;
};

}  // namespace cursiv
