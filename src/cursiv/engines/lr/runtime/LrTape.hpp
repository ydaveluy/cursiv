// The stream's single writer: every record goes straight to the tree's
// stream, in the order the elected derivation writes it.
#pragma once

#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

// No record is waiting for the state it settles in.
inline constexpr NodeCount kLrNoPending = 0xFFFFFFFFu;
// A close that names no state yet: the cascade it ends in settles it.
inline constexpr std::uint16_t kLrNoState = 0xFFFFu;

class LrTape {
 public:
  explicit LrTape(CstRun& trunk) : _trunk(trunk) {}

  LrTape(const LrTape&) = delete;
  LrTape& operator=(const LrTape&) = delete;

  // The record count, so where the next record starts: what a frame keeps to
  // name the row of the record it holds.
  [[nodiscard]] NodeCount len() const { return _trunk.records(); }

  // Where the last record ends: what the tokens hold, trivia past it plain.
  [[nodiscard]] TextOffset held() const {
    const NodeCount n = len();
    return n == 0 ? 0 : at(static_cast<std::uint32_t>(n - 1)).end;
  }

  [[nodiscard]] const CstNode& at(std::uint32_t word) const {
    return _trunk.at(word);
  }

  template <StampLayout L>
  CURSIV_ALWAYS_INLINE void rename(std::uint32_t word, ElementId element) {
    L.set_row(_trunk.at(word), element);
  }

  template <StampLayout L>
  CURSIV_ALWAYS_INLINE void leaf(TextOffset begin, TextOffset end,
                                 ElementId element, std::uint16_t state) {
    _trunk.leaf(begin, end, L.make(element, state));
  }

  template <StampLayout L>
  CURSIV_ALWAYS_INLINE void hidden(TextOffset begin, TextOffset end,
                                   ElementId element) {
    _trunk.hidden(begin, end, L.make(element, 0));
  }

  void repaired_leaf(TextOffset begin, TextOffset end, std::uint32_t engine) {
    _trunk.repaired_leaf(begin, end, engine);
  }
  void repaired_close(TextOffset begin, TextOffset end, NodeCount extent,
                      std::uint32_t engine) {
    _trunk.repaired_close(begin, end, extent, engine);
  }

  template <StampLayout L = StampLayout{}>
  CURSIV_ALWAYS_INLINE void exit_anchored(NodeCount from, TextOffset begin,
                                          TextOffset end, ElementId element,
                                          std::uint16_t state = kLrNoState) {
    const NodeCount at = len();
    _pend = state == kLrNoState ? at : kLrNoPending;
    _trunk.close(begin, end, at - from + 1,
                 L.make(element, state == kLrNoState ? 0 : state));
  }

  // One record over `[from, last]`, slipped in before what follows it: a
  // receiver of several records, grouped once its turn closes.
  template <StampLayout L>
  void group(NodeCount from, NodeCount last, TextOffset begin) {
    const NodeCount at = last + 1;
    _trunk.insert(at, begin, _trunk.at(last).end, at - from + 1,
                  L.make(kNoElement, 0));
    if (_pend != kLrNoPending && _pend >= at) ++_pend;
  }

  // The cascade ended here: whatever it closed is answered from this state.
  // Nothing pending outside one, and nothing at all where the document ran
  // out before a token was read again.
  template <StampLayout L = StampLayout{}>
  CURSIV_ALWAYS_INLINE void settle(std::uint16_t state) {
    if (_pend == kLrNoPending) [[unlikely]]
      return;
    L.set_state(_trunk.at(_pend), state);
    _pend = kLrNoPending;
  }

  NodeCount pending_close() const { return _pend; }
  void pending_close(NodeCount at) { _pend = at; }

 private:
  CstRun& _trunk;
  // The record a cascade of reductions closed last, still owed its state.
  NodeCount _pend = kLrNoPending;
};

}  // namespace cursiv
