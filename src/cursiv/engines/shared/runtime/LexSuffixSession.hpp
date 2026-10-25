// Predicate searches of one immutable document share their DFA suffixes.
// A suffix belongs to its state and text position, including the boundaries
// it inspected. Token-head refusals and echoing relays still use the matcher.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
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

  // Other buffers and shorter views (a section's second reading) fall back.
  template <class Fallback>
  static Scan match(const Tape& tape, const char* src, TextOffset len,
                    std::uint16_t mode, TextOffset from, TextOffset* seen,
                    Fallback&& fallback) {
    if constexpr (Tape::kRefuses) {
      return fallback();
    } else {
      LexSuffixSession* const session = active();
      if (session == nullptr || src != session->_src || len != session->_len)
        return fallback();
      if (!session->_cache) session->_cache.emplace();
      Scan out{from, kInvalidSymbol};
      TextOffset halt = from;
      if (!session->_cache->match(tape, src, len, mode, from, out, halt))
        return fallback();
      if (seen) *seen = std::max(*seen, halt < len ? halt + 1 : len);
      return out;
    }
  }

 private:
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
               std::uint16_t mode, TextOffset from, Scan& out,
               TextOffset& halt) {
      auto state = tape.start(mode);
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
  std::optional<Cache> _cache;
};

}  // namespace cursiv
