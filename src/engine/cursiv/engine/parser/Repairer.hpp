// The machine a token at a time: the language's steps write on it a token
// the text does not hold as written, and come back once it is shifted.
#pragma once

#include <algorithm>
#include <cstdint>

#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Tables.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

struct Repairer {
  // `settles`: what waits to be written goes before the token even where
  // it holds no text. `repairs`: the token is one a repair wrote, not one the
  // text holds as read.
  Repairer(Machine& machine, bool settles, bool repairs = true)
      : m(machine),
        resume(machine.resume),
        at(machine.at),
        tok(machine.tok),
        indented(machine.indented),
        settles(settles),
        repairs(repairs) {}

  static constexpr bool kSteps = true;
  // Out of line, each of them: a repair is written once, and its code is
  // kept small.
  CURSIV_ALWAYS_INLINE Machine& lexing() { return m; }
  CURSIV_NOINLINE void push(std::uint32_t state, NodeCount open,
                             Token = {nullptr, 0}) {
    m.push(state, open);
  }
  CURSIV_NOINLINE NodeCount pop(std::uint32_t count, std::uint32_t) { return m.pop(count); }
  CURSIV_NOINLINE std::uint32_t under(std::uint32_t depth = 0) const { return m.under(depth); }
  CURSIV_NOINLINE NodeCount here(std::uint32_t prod) const { return m.here(prod); }
  // What waits is written when the token is shifted, past the reductions it
  // makes: `shift` says.
  CURSIV_ALWAYS_INLINE void settle() {}
  static constexpr bool flushed() { return true; }
  CURSIV_ALWAYS_INLINE bool roomy(bool frame, bool record) const { return m.roomy(frame, record); }
  void make_room() { m.make_room(); }
  CURSIV_NOINLINE void forget(const char* from) { m.forget(from); }
  // The token's leaf and its frame. A token with no text stands where the
  // text was written up to.
  CURSIV_NOINLINE void shift(std::uint32_t state, std::uint32_t entry, Token token) {
    TextOffset begin = token.begin(m);
    auto end = static_cast<TextOffset>(token.end - m.src);
    if (begin != end || settles) m.settle();
    const NodeCount open = m.tape.records();
    if (begin == end && open != 0) begin = end = std::max(end, m.tape.at(open - 1).end);
    if (repairs)
      m.tape.repairedLeaf(begin, end, entry);
    else
      m.tape.leaf(begin, end, entry);
    m.push(state, open);
    m.at = m.src + end;
  }
  template <unsigned Shapes, class Fuse = std::nullptr_t>
  CURSIV_NOINLINE NodeCount close(NodeCount open, std::uint32_t entry, const char* where,
                                  Fuse fuse = nullptr) {
    if constexpr (Shapes == kRecordEmpty) {
      m.settle();
      m.make_room();
    }
    return m.template close<Shapes>(open, entry, where, fuse);
  }
  template <class Fuse>
  CURSIV_ALWAYS_INLINE void take(std::uint32_t at, std::uint32_t size, Fuse&& fuse) {
    m.take(at, size, fuse);
  }
  CURSIV_ALWAYS_INLINE void accept() { m.accept(); }
  CURSIV_ALWAYS_INLINE Exit shifted() { return m.shifted(); }
  Exit refused(Token held) { return m.refused(held); }
  Exit split(Token held, std::uint32_t first, std::uint32_t count,
             std::uint32_t rereads = 0) {
    return m.split(held, first, count, rereads);
  }

  Machine& m;
  // The machine's own: where it goes on from, the text read, the token in
  // hand.
  std::uint32_t& resume;
  const char*& at;
  Token& tok;
  Indented& indented;
  bool settles;
  bool repairs;
};

}  // namespace cursiv::engine
