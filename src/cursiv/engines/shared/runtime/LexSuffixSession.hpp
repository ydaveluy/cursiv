// Lexical searches of one immutable document share their DFA suffixes.
// A suffix belongs to its state and text position, including the boundaries
// it inspected. Echoing relays, and a mode that reads a refusing terminal,
// still use the matcher. The conditions read over the document share the
// rounds of their runs.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
#include "cursiv/engines/shared/runtime/PredicateRuns.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

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
  template <class Fallback>
  static Scan match(const Tape& tape, const char* src, TextOffset len,
                    std::uint16_t mode, TextOffset from, TextOffset* seen,
                    Fallback&& fallback) {
    LexSuffixSession* const session = active();
    if (session == nullptr || src != session->_src || len != session->_len ||
        !shares(tape, mode))
      return fallback();
    if (!session->_cache) session->_cache.emplace();
    Scan out{from, kInvalidSymbol};
    TextOffset halt = from;
    if (!session->_cache->match(tape, src, len, tape.start(mode), from, out,
                                halt))
      return fallback();
    if (seen) *seen = std::max(*seen, halt < len ? halt + 1 : len);
    return out;
  }

  // The answers of the conditions' runs over this document; none for any
  // other text.
  static PredicateRuns* runs(const char* src, TextOffset len) {
    LexSuffixSession* const session = active();
    return session != nullptr && src == session->_src && len == session->_len
               ? &session->_runs
               : nullptr;
  }

  struct Continued {
    Scan best;
    TextOffset halt = 0;
    bool available = false;
  };

  CURSIV_ALWAYS_INLINE static bool has_backtracked() {
    const auto* session = active();
    return session != nullptr && session->_backtracking;
  }

  // Generated walks ask only where they enter a cycle that may leave a long
  // unmatched tail. Returning by value keeps the fast walk's locals in
  // registers; a token that never enters such a state makes no cache call.
  CURSIV_NOINLINE static Continued continue_at(
      Tape tape, const char* src, TextOffset len, std::uint16_t state,
      TextOffset at) {
    LexSuffixSession* const session = active();
    if (session == nullptr || !session->_backtracking ||
        src != session->_src || len != session->_len)
      return {};
    Continued out;
    out.best.end = at;
    out.halt = at;
    out.available = session->_cache->match(tape, src, len, state, at,
                                           out.best, out.halt);
    return out;
  }

  // The caller has found the walk read more than `kBacktrackTail` bytes past
  // its match (`out`, a real match, `halt` beyond its end): the suffix is
  // worth sharing from here on.
  CURSIV_NOINLINE static Scan finish(
      Tape tape, const char* src, TextOffset len, std::uint16_t state,
      TextOffset from, [[maybe_unused]] TextOffset halt, Scan out) {
    assert(halt > out.end && halt - out.end > kBacktrackTail);
    remember(tape, src, len, state, from);
    return out;
  }

 private:
  CURSIV_NOINLINE static void remember(Tape tape, const char* src,
                                       TextOffset len, std::uint16_t state,
                                       TextOffset from) {
    LexSuffixSession* const session = active();
    if (session == nullptr || src != session->_src || len != session->_len)
      return;
    if (!session->_cache) session->_cache.emplace();
    Scan out{from, kInvalidSymbol};
    TextOffset halt = from;
    if (session->_cache->match(tape, src, len, state, from, out, halt))
      session->_backtracking = true;
  }

  struct Answer {
    // An invalid match has no endpoint: it must not replace the caller's
    // earlier acceptance or change the start of a wholly failed walk.
    Scan best{0, kInvalidSymbol};
    TextOffset halt = 0;
  };
  static Answer followed(Answer prior, const Answer& next) {
    if (next.best.term != kInvalidSymbol) prior.best = next.best;
    prior.halt = std::max(prior.halt, next.halt);
    return prior;
  }
  struct Cache {
    static constexpr TextOffset kStride = 16;
    struct Stood {
      std::uint64_t key;
      Answer local;
    };
    std::unordered_map<std::uint64_t, Answer> known;
    std::vector<Stood> path;

    bool match(const Tape& tape, const char* src, TextOffset len,
               std::uint16_t state, TextOffset from, Scan& out,
               TextOffset& halt) {
      TextOffset at = from;
      Answer head, tail;
      path.clear();
      for (;;) {
        if (at % kStride == 0) {
          const std::uint64_t key = (std::uint64_t{at} << 16) | state;
          if (const auto it = known.find(key); it != known.end()) {
            tail = it->second;
            break;
          }
          path.push_back({key, {}});
        }
        // A relay may capture text from the token's start. Such a suffix
        // cannot be shared with a token that began elsewhere.
        if constexpr (requires { tape.relay(state); })
          if (tape.relay(state) != kNoRelay) return false;
        Answer& local = path.empty() ? head : path.back().local;
        LexMatch taken{at, kInvalidSymbol};
        taken.take(tape, state, src, at, len, local.halt);
        if (taken.term != kInvalidSymbol)
          local.best = Scan{taken.end, taken.term};
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
      for (auto it = path.rbegin(); it != path.rend(); ++it) {
        tail = followed(it->local, tail);
        known.emplace(it->key, tail);
      }
      tail = followed(head, tail);
      out = tail.best.term == kInvalidSymbol ? Scan{from, kInvalidSymbol}
                                             : tail.best;
      halt = tail.halt;
      return true;
    }
  };

  static LexSuffixSession*& active() {
    thread_local LexSuffixSession* session = nullptr;
    return session;
  }
  const char* _src;
  TextOffset _len;
  LexSuffixSession* _previous;
  bool _backtracking = false;
  std::optional<Cache> _cache;
  PredicateRuns _runs;
};

}  // namespace cursiv
