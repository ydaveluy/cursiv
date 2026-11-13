// Lexical searches of one immutable document share their DFA suffixes, each
// keyed by its state and position, and the rounds of their conditions' runs.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/LexMatch.hpp"
#include "cursiv/shared/runtime/PredicateRuns.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// The DFA suffixes read of one text: a walk keeps a point every `kStride`
// bytes, and a later walk that reaches one in the same state stops there. A
// cache keeps one kind of walk: all with the same `Fails`.
class LexSuffixCache {
 public:
  static constexpr TextOffset kStride = 16;

  struct Read {
    Scan best;
    TextOffset halt = 0;
    bool available = false;
  };

  // What the walk from `state` at `from` matches, and where it stops reading.
  // It keeps points from `keyed` on and calls `turn` at each. Unavailable
  // where the full matcher answers: at a relay that echoes or whose assertion
  // holds and, with `Fails`, at any acceptance.
  template <bool Fails, class Tape>
  Read match(Tape tape, const char* src, TextOffset len, std::uint16_t state,
             TextOffset from, TextOffset keyed,
             function_ref<void(TextOffset)> turn) {
    TextOffset at = from;
    Answer head, tail;
    _path.clear();
    for (;;) {
      if (at >= keyed && at % kStride == 0) {
        if (const Answer* const known =
                kept((std::uint64_t{at} << 16) | state)) {
          tail = *known;
          break;
        }
        turn(at);
      }
      Answer& local = _path.empty() ? head : _path.back().local;
      if constexpr (Tape::kRelays)
        for (auto k = tape.relay(state); k != kNoRelay;
             k = tape.relay_of(k).next) {
          const LexRelay& relay = tape.relay_of(k);
          TextOffset seen = at;
          if (relay.asserts == nullptr || relay.asserts(src, len, at, seen))
            return {};
          local.halt = std::max(local.halt, seen);
        }
      if constexpr (Fails) {
        if (tape.accept(state) != 0) return {};
      } else {
        LexMatch taken{at, kInvalidSymbol};
        taken.take(tape, state, src, at, len, local.halt);
        if (taken.term != kInvalidSymbol)
          local.best = Scan{taken.end, taken.term};
      }
      if (at >= len) {
        tail.halt = len;
        break;
      }
      const auto next = tape.step(state, static_cast<unsigned char>(src[at]));
      if (next == kDeadState) {
        tail.halt = at;
        break;
      }
      state = next;
      ++at;
    }
    if (!_path.empty()) tail = settled(tail);
    tail = followed(head, tail);
    return {tail.best.term == kInvalidSymbol ? Scan{from, kInvalidSymbol}
                                             : tail.best,
            tail.halt, true};
  }

 private:
  struct Answer {
    // An invalid match has no endpoint: it must not replace the caller's
    // earlier acceptance or change the start of a wholly failed walk.
    Scan best{0, kInvalidSymbol};
    TextOffset halt = 0;
  };
  // A point kept, and what the walk read from it up to the next one.
  struct Point {
    std::uint64_t key;
    Answer local;
  };
  static Answer followed(Answer prior, const Answer& next) {
    if (next.best.term != kInvalidSymbol) prior.best = next.best;
    prior.halt = std::max(prior.halt, next.halt);
    return prior;
  }
  // The answer of the point `key`; null where it has none yet, and the walk
  // keeps it.
  CURSIV_NOINLINE const Answer* kept(std::uint64_t key) {
    if (const Answer* const answer = _known.find(key)) return answer;
    _path.push_back({key, {}});
    return nullptr;
  }
  // The answer of each point the walk kept, back from `tail`: the first one's.
  CURSIV_NOINLINE Answer settled(Answer tail) {
    for (auto it = _path.rbegin(); it != _path.rend(); ++it) {
      tail = followed(it->local, tail);
      _known.emplace(it->key).first = tail;
    }
    return tail;
  }
  FlatMap<Answer> _known;
  std::vector<Point> _path;
};

template <class Tape>
class LexSuffixSession {
 public:
  LexSuffixSession(const char* src, TextOffset len)
      : _src(src), _len(len), _previous(std::exchange(active(), this)) {}
  ~LexSuffixSession() { active() = _previous; }
  LexSuffixSession(const LexSuffixSession&) = delete;
  LexSuffixSession& operator=(const LexSuffixSession&) = delete;

  // Whether walks of `mode` share suffixes. A refusal answers for its
  // token's head, which a suffix does not see: in a table that refuses words,
  // only the modes the generator marks share them (`LexBacktrack.hpp`).
  static bool shares(const Tape& tape, std::uint16_t mode) {
    if constexpr (Tape::kRefuses)
      return tape.backtracks(mode);
    else
      return true;
  }

  // Other buffers and shorter views (a section's second reading) fall back.
  // A read keeps no point in its first `kStride` bytes: a short token is
  // walked, not looked up.
  template <class Fallback>
  static Scan match(const Tape& tape, const char* src, TextOffset len,
                    std::uint16_t mode, TextOffset from, TextOffset* seen,
                    Fallback&& fallback) {
    LexSuffixSession* const session = active();
    if (session == nullptr || src != session->_src || len != session->_len ||
        !shares(tape, mode))
      return fallback();
    const auto read = session->read(tape, tape.start(mode), from,
                                    from + LexSuffixCache::kStride);
    if (!read.available) return fallback();
    if (seen) *seen = std::max(*seen, read.halt < len ? read.halt + 1 : len);
    return read.best;
  }

  // The answers of the conditions' runs over this document; none for any
  // other text.
  static PredicateRuns* runs(const char* src, TextOffset len) {
    LexSuffixSession* const session = active();
    return session != nullptr && src == session->_src && len == session->_len
               ? &session->_runs
               : nullptr;
  }

  CURSIV_ALWAYS_INLINE static bool has_backtracked() {
    const auto* session = active();
    return session != nullptr && session->_backtracking;
  }

  // Generated walks ask only where they enter a cycle that may leave a long
  // unmatched tail. Returning by value keeps the fast walk's locals in
  // registers; a token that never enters such a state makes no cache call.
  CURSIV_NOINLINE static LexSuffixCache::Read continue_at(
      Tape tape, const char* src, TextOffset len, std::uint16_t state,
      TextOffset at) {
    LexSuffixSession* const session = active();
    if (session == nullptr || !session->_backtracking ||
        src != session->_src || len != session->_len)
      return {};
    return session->read(tape, state, at, at);
  }

  // Shares the suffix of a walk begun at `from` in `state` that stopped at
  // `halt`, more than `kBacktrackTail` bytes past its match `out`. Answers
  // `out`.
  CURSIV_ALWAYS_INLINE static Scan finish(Tape tape, const char* src,
                                          TextOffset len, std::uint16_t state,
                                          TextOffset from, TextOffset halt,
                                          Scan out) {
    if (out.term != kInvalidSymbol && halt > out.end &&
        halt - out.end > kBacktrackTail)
      return shared(tape, src, len, state, from, out);
    return out;
  }

 private:
  // The walk from `state` at `from` over this document's suffixes. It reads
  // the parse's stop at one point in every `kTurns`.
  LexSuffixCache::Read read(Tape tape, std::uint16_t state, TextOffset from,
                            TextOffset keyed) {
    return _cache.match<false>(
        tape, _src, _len, state, from, keyed, [](TextOffset at) {
          if (at % (LexSuffixCache::kStride * CancellationPoll::kTurns) == 0)
              [[unlikely]]
            read_stop();
        });
  }
  CURSIV_COLD CURSIV_NOINLINE static void read_stop() {
    ThreadStop::poll().check();
  }
  // `out`, the suffix of its walk kept for the walks to come.
  CURSIV_NOINLINE static Scan shared(Tape tape, const char* src, TextOffset len,
                                     std::uint16_t state, TextOffset from,
                                     Scan out) {
    if (LexSuffixSession* const session = active();
        session != nullptr && src == session->_src && len == session->_len &&
        session->read(tape, state, from, from).available)
      session->_backtracking = true;
    return out;
  }

  static LexSuffixSession*& active() {
    thread_local LexSuffixSession* session = nullptr;
    return session;
  }
  const char* _src;
  TextOffset _len;
  LexSuffixSession* _previous;
  bool _backtracking = false;
  LexSuffixCache _cache;
  PredicateRuns _runs;
};

}  // namespace cursiv::engine
