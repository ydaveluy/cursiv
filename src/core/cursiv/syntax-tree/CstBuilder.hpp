// How a tree is written, and the only writer of a `Cst`: enter/exit,
// leaf/hidden, and the `bind_*` calls that finish a pass. Leaf and walk reads
// index the tree once, so they come after the build.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv {

struct Grammar;

namespace detail {

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

  // Ends never decrease, hidden records included: a parent goes right after
  // its last visible descendant, before any trailing trivia, and ends where
  // that descendant ends.
  NodeId exit(Open from, TextOffset begin, std::uint32_t engine) {
    assert(from._count <= _cst.size() &&
           "a CST parent cannot start after its children");
    const NodeId id = lastVisibleNodeId + 1;
    if (id <= from._count) [[unlikely]]
      return exit_empty(from, begin, engine);
    // Recovery may insert a child before the token at which its parent was
    // opened. The parent still covers that child, including an insertion
    // before ignored trailing whitespace at the end of a partial document.
    if (const TextOffset first = _cst._stream.at(from._count).begin;
        begin > first)
      begin = first;
    const TextOffset end = _cst._stream.at(lastVisibleNodeId).end;
    const NodeCount extent = id - from._count + 1;
    if (id == _cst.size()) [[likely]]
      _cst._stream.close(begin, end, extent, engine);
    else
      _cst._stream.insert(id, begin, end, extent, engine);
    lastVisibleNodeId = id;
    return id;
  }

  // Inlined like the run's own writers: an engine writes one per token.
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

  // A repair, appended by the attempt that survived. `Cst::recovered()` reads
  // this list.
  void note_repair(DiagnosticKind kind, ElementId element, TextOffset begin,
                   TextOffset end, std::vector<ElementId> also = {},
                   bool mixedIndent = false) {
    // Two identical repairs in a row are one fault: nested constructs closing
    // at one point expect the same closer.
    if (!_cst._diagnostics.empty()) {
      const ParseDiagnostic& last = _cst._diagnostics.back();
      if (last.kind == kind && last.element == element && last.begin == begin &&
          last.end == end && last.also == also &&
          last.mixedIndent == mixedIndent)
        return;
    }
    _cst._diagnostics.push_back(
        {kind, mixedIndent, element, std::move(also), begin, end, {}});
  }

  // Where an engine writes its records, and where every reader reads them:
  // one run, indexed by node id.
  CstRun& stream() { return _cst._stream; }
  const CstRun& stream() const { return _cst._stream; }
  // Marks the records as written by an engine.
  void bind_stream() { _cst.bindStream(); }
  void reserve_stream(std::size_t words) { _cst._stream.reserve_for(words); }

  // Set by a completed pass. A root with no record binds nothing, and the AST
  // build then answers `no_entry` rather than reading past the end.
  void bind_entry(NodeId begin, NodeId end, NodeId root) noexcept {
    const bool in_range = root == kNoNode || root < _cst.size();
    _cst.bindEntry(begin, end, in_range ? root : kNoNode);
  }
  void bind_accepted(bool accepted) noexcept { _cst.bindAccepted(accepted); }
  void bind_model(const Grammar& grammar, OwnedNodes owned) noexcept {
    _cst.bindModel(grammar, owned);
  }
  void bind_translation(std::string_view document,
                        std::unique_ptr<SourceTranslation> translation) {
    _cst.bindTranslation(document, std::move(translation));
  }

 private:
  // The empty composite goes at its region's open, not at the run's end, so
  // the run stays in end order and the enclosing exit comes after it.
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

}  // namespace detail

}  // namespace cursiv
