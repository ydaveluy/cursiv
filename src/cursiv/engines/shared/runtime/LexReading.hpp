// A token the lexer refused in its middle, read on the mode's automaton as a
// superset: the walk goes back into the body it was reading, so a fault
// inside the token never moves where it ends. Read off the emitted table; a
// valid text never reaches it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/engines/shared/runtime/LexRelay.hpp"
#include "cursiv/engines/shared/runtime/LexWalk.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct LexReading {
  // `kInvalidSymbol`: no body owing a closer was entered here.
  SymbolId term = kInvalidSymbol;
  // Where its body began: past its opener.
  TextOffset open = 0;
  TextOffset end = 0;
  // Ended on its own closer; otherwise cut where its body refused a line end,
  // or at the end of the text.
  bool closed = false;
};

// The automaton states that loop on a byte without accepting, the terminal a
// state owes where its body runs on to a closer, and the bytes a mode opens
// such a body with.
struct LexOwed {
  const std::uint8_t* bodies = nullptr;
  const std::uint16_t* owed = nullptr;
  const ByteSet* openers = nullptr;
};

// Where a line ends: a `\n` or a `\r`.
inline bool lex_line_end_at(const char* src, TextOffset i) {
  return src[i] == '\n' || src[i] == '\r';
}

// Whether the relays from `k` hold an echo: an assertion opens nothing.
template <class Tape>
bool lex_echoes(const Tape& tape, std::uint16_t k) {
  for (auto r = k; r != kNoRelay; r = tape.relay_of(r).next)
    if (!tape.relay_of(r).asserts && !tape.relay_of(r).recognizes) return true;
  return false;
}

// A relay met at `i`, walking from `from`: the terminal it reads to the end of
// the text where no search finds its closer and no recognition ends, else
// none. `seen` rises to how far the recognitions read.
template <class Tape>
SymbolId lex_relay_open(const Tape& tape, const char* src, TextOffset len,
                        std::uint16_t k, TextOffset from, TextOffset i,
                        TextOffset& seen) {
  SymbolId open = kInvalidSymbol;
  for (auto r = k; r != kNoRelay; r = tape.relay_of(r).next) {
    if (tape.relay_of(r).recognizes) {
      TextOffset at = i;
      const bool ends =
          !tape.relay_of(r).recognizes(src, len, from, at).empty();
      if (at > seen) seen = at;
      if (ends) return kInvalidSymbol;
      if (at >= len && open == kInvalidSymbol) open = tape.relay_of(r).term;
      continue;
    }
    if (tape.relay_of(r).asserts) continue;
    if (open == kInvalidSymbol) open = tape.relay_of(r).term;
    const auto e = lex_relay(
        r, tape.relay_of(r), tape.sets(), tape.names(), src, len, from, i,
        [&](std::uint16_t, std::uint16_t resume, TextOffset p) {
          return lex_walk_from(tape, src, len, resume, p);
        });
    if (e != 0) return kInvalidSymbol;
  }
  return open;
}

// The state the first assertion among the relays from `k` that holds at `i`
// resumes in, `kDeadState` where none does. `seen` rises to how far they read.
template <class Tape>
std::uint16_t lex_hop(const Tape& tape, std::uint16_t k, const char* src,
                      TextOffset len, TextOffset i, TextOffset& seen) {
  for (auto r = k; r != kNoRelay; r = tape.relay_of(r).next)
    if (tape.relay_of(r).asserts != nullptr &&
        tape.relay_of(r).asserts(src, len, i, seen))
      return tape.relay_of(r).resume;
  return kDeadState;
}

// The reading at `p` under `mode`, where the lexer's walk read past its match
// and was refused. From the body it last entered, a refused byte resumes the
// body: the byte itself where the body admits it -- an escape given up -- and
// the byte after it otherwise, save a line end, which the token stops short
// of. A token open to the end of the text stops there. `reached` rises to how
// far the walks read; `match` is the engine's walk with its halt.
template <class Tape, class Match>
LexReading lex_reading(const Tape& tape, const LexOwed& derived, Match&& match,
                       const char* src, TextOffset len, std::uint16_t mode,
                       TextOffset p, TextOffset& reached) {
  LexReading out;
  if (p >= len ||
      !derived.openers[mode].has(static_cast<unsigned char>(src[p])))
    return out;
  const auto read = [&reached](TextOffset x) {
    if (x > reached) reached = x;
  };
  TextOffset halt = p;
  const Scan m = match(src, len, mode, p, halt);
  read(halt);
  if (halt <= m.end) return out;
  std::uint16_t s = tape.start(mode);
  std::uint16_t body = kDeadState;
  TextOffset i = p;
  // What the lexer took before the refusal is its own: only a body entered
  // past its last acceptance owes a closer. An echo ends the walk, and so
  // does a recognition that reads to the end of the text unmatched: the
  // token is open to the end where nothing closes it. Where the automaton
  // stops, the walk goes on in the rest of an assertion that holds there.
  bool relayed = false;
  const auto replay = [&](bool resets) {
    [[maybe_unused]] std::size_t hops = 0;
    for (;;) {
      [[maybe_unused]] std::uint16_t k = kNoRelay;
      if constexpr (requires { tape.relay(s); }) {
        k = tape.relay(s);
        if (k != kNoRelay) {
          TextOffset seen = i;
          const SymbolId open = lex_relay_open(tape, src, len, k, p, i, seen);
          read(seen);
          if (open != kInvalidSymbol || lex_echoes(tape, k)) {
            relayed = true;
            out.term = open;
            out.open = i;
            out.end = len;
            read(len);
            return;
          }
        }
      }
      if (tape.accept(s) != 0) {
        if (resets) body = kDeadState;
      } else if (derived.bodies[s] != 0) {
        if (body == kDeadState) out.open = i;
        body = s;
      }
      std::uint16_t nx = i < len
                             ? tape.step(s, static_cast<unsigned char>(src[i]))
                             : kDeadState;
      if (nx != kDeadState) {
        s = nx;
        ++i;
        hops = 0;
        continue;
      }
      if constexpr (requires { tape.relay(s); }) {
        if (hops < tape.relay_count()) {
          TextOffset seen = i;
          nx = lex_hop(tape, k, src, len, i, seen);
          read(seen);
          if (nx != kDeadState) ++hops;
        }
      }
      if (nx == kDeadState) return;
      s = nx;
    }
  };
  replay(true);
  read(i);
  if (relayed) return out;
  if (body == kDeadState) return out;
  for (;;) {
    const bool admits =
        i < len &&
        tape.step(body, static_cast<unsigned char>(src[i])) != kDeadState;
    if (i >= len || (!admits && lex_line_end_at(src, i))) {
      out.term = derived.owed[body];
      out.end = i;
      return out;
    }
    const TextOffset from = admits ? i : i + 1;
    TextOffset walked = from;
    const Scan r = lex_walk_from(tape, src, len, body, from, walked);
    read(walked);
    if (r.term != kInvalidSymbol) {
      out.term = r.term;
      out.end = r.end;
      out.closed = true;
      return out;
    }
    s = body;
    i = from;
    replay(false);
    read(i);
    if (relayed) return out;
  }
}

// Whether an end at `e` would cut a scalar, or a word, in two.
template <const auto& C>
bool lex_cuts(const char* src, TextOffset len, TextOffset e) {
  if (e >= len) return false;
  if ((static_cast<unsigned char>(src[e]) & 0xC0u) == 0x80u) return true;
  if (e == 0 || !lex_boundary(C).word_at(src, e, len)) return false;
  TextOffset b = e - 1;
  while (b > 0 && (static_cast<unsigned char>(src[b]) & 0xC0u) == 0x80u) --b;
  return lex_boundary(C).word_at(src, b, len);
}

// What a deletion takes of the reading at `p`: a closed token whole; of one
// that never closed, only its opener -- where its body would have ended is
// what the re-readings ask of the text after it.
inline TextOffset lex_cut(TextOffset p, const LexReading& r) {
  if (r.closed) return r.end;
  return r.open > p ? r.open : p + 1;
}

}  // namespace cursiv
