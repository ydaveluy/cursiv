// The rests of the tokens a walk handed to their searches, read once the walk
// is done: one loop over positions, and each state read once per position
// and held match, however many relays reached it. A relay inside a
// repetition costs its bytes: its searches keep what they settled.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/LexMarkRun.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
#include "cursiv/engines/shared/runtime/LexNestedSearch.hpp"
#include "cursiv/engines/shared/runtime/LexRelay.hpp"
#include "cursiv/engines/shared/runtime/PredicateState.hpp"
#include "cursiv/engines/shared/runtime/SmallRun.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// `Ad` is the walk's own table adapter. Every rest a relay resumes reads its
// relay's terminal and nothing else, so what a rest answers is how far that
// terminal reaches, and the furthest reach per terminal is all a walk keeps.
template <class Ad>
class LexRelayRun {
 public:
  LexRelayRun() = default;
  LexRelayRun(const LexRelayRun&) = delete;
  LexRelayRun& operator=(const LexRelayRun&) = delete;

  bool any() const { return _start != kIdle; }

  // A relay reached at `at`, in a token begun at `start`: the search runs
  // now, the rest past the echo waits for `finish`.
  CURSIV_ALWAYS_INLINE void launch(const Ad& ad, std::uint16_t k,
                                   const char* src, TextOffset len,
                                   TextOffset start, TextOffset at) {
    _start = start;
    const LexRelay& r = ad.relay_of(k);
    resumes(ad, k, src, len, at,
            [&](TextOffset p) { push(p, r.resume, r.tier); });
  }

  // Every rest read to its end: `take(term, end, tier)` hears each reading
  // that stands, `tier` saying what branch read it, and `far` covers what the
  // searches and the rests read.
  template <class Take>
  CURSIV_NOINLINE void finish(const Ad& ad, const char* src, TextOffset len,
                              TextOffset& far, Take take) {
    while (!_roots.empty()) {
      const Root r = pop();
      Cursor c{r.state, fresh(r.at), r.tier};
      TextOffset i = r.at;
      // One cursor reads alone until a root or a relay stands where it reads.
      for (;;) {
        if ((!_roots.empty() && _roots.begin()->at == i) ||
            ad.relay(c.state) != kNoRelay) {
          if (!together(ad, src, len, far, take, c, i)) break;
          continue;
        }
        c.m.take(ad, c.state, src, i, len, far);
        const std::uint16_t to =
            i < len ? ad.step(c.state, static_cast<unsigned char>(src[i]))
                    : kDeadState;
        if (to == kDeadState) {
          far = std::max(far, i);
          if (c.m.term != kInvalidSymbol)
            take(c.m.term, c.m.end, unsigned{c.tier});
          break;
        }
        const bool self = c.state == to;
        c.state = to;
        ++i;
        if (self && lex_run_blind(ad, to)) {
          const TextOffset stop =
              _roots.empty() ? len : std::min(len, _roots.begin()->at);
          i = ad.run(to, src, stop, i);
        }
      }
    }
    far = std::max(far, _seen);
    _seen = 0;
    _sought = false;
    _start = kIdle;
    _memo.reset();
  }

 private:
  struct Root {
    TextOffset at;
    std::uint16_t state;
    std::uint8_t tier;
  };
  struct Cursor {
    std::uint16_t state;
    LexMatch m;
    std::uint8_t tier;
  };
  using Cursors = SmallRun<Cursor, 4>;
  static constexpr auto kLater = [](const Root& x, const Root& y) {
    return x.at > y.at;
  };
  static LexMatch fresh(TextOffset i) {
    return LexMatch{i, kInvalidSymbol};
  }
  // What one relay's earlier searches of the walk settled.
  struct Memo {
    std::uint16_t relay;
    LexNestedSearch nested;
    LexMarkRun mark;
  };

  // The positions relay `k`, reached at `at`, resumes from; `found` hears each.
  // A search that finds no closer read to the end looking for one.
  template <class Found>
  CURSIV_ALWAYS_INLINE void resumes(const Ad& ad, std::uint16_t k,
                                    const char* src, TextOffset len,
                                    TextOffset at, Found found) {
    const LexRelay& r = ad.relay_of(k);
    if (r.recognizes) {
      const auto ends = r.recognizes(src, len, _start, _seen);
      for (std::size_t i = 0; i < ends.size(); ++i) found(ends[i].at);
    } else if (r.asserts) {
      if (r.asserts(src, len, at, _seen)) found(at);
    } else if (const TextOffset p = seek(ad, k, src, len, at)) {
      found(p);
    } else {
      _seen = len;
    }
  }

  // The walk's first search keeps nothing, as most walks launch one; the ones
  // after it are reading the same text from another opener, and do.
  CURSIV_NOINLINE TextOffset seek(const Ad& ad, std::uint16_t k,
                                  const char* src, TextOffset len,
                                  TextOffset at) {
    const LexRelay& r = ad.relay_of(k);
    if (!std::exchange(_sought, true))
      return lex_relay_seek(r, ad.sets(), ad.names(), src, len, _start, at);
    Memo& m = memo_of(k);
    if (r.nested) return m.nested.seek(r, ad.names(), src, len, at - r.back);
    if (at < _start || at - _start < r.fence) return 0;
    return lex_echo_seek(
        r, ad.sets(), ad.names(), src, len, at,
        m.mark.begin(ad.sets()[r.markSet], src, _start, at - r.fence));
  }

  // The memo of relay `k`, made when first asked for: a walk reaches few of
  // the grammar's relays, and only those hold one.
  Memo& memo_of(std::uint16_t k) {
    if (_memo == nullptr) _memo = std::make_unique<std::vector<Memo>>();
    auto at = std::lower_bound(
        _memo->begin(), _memo->end(), k,
        [](const Memo& m, std::uint16_t relay) { return m.relay < relay; });
    if (at == _memo->end() || at->relay != k)
      at = _memo->insert(at, Memo{k, {}, {}});
    return *at;
  }

  CURSIV_ALWAYS_INLINE void push(TextOffset at, std::uint16_t state,
                                 std::uint8_t tier) {
    _roots.push_back(Root{at, state, tier});
    std::push_heap(_roots.begin(), _roots.end(), kLater);
  }
  CURSIV_ALWAYS_INLINE Root pop() {
    std::pop_heap(_roots.begin(), _roots.end(), kLater);
    const Root r = _roots.end()[-1];
    _roots.pop_back();
    return r;
  }

  // The cursors in step from `i`, `c` among them and not yet read there:
  // false once every one has ended, true with the lone survivor in `c` at
  // `i` where it reads alone again.
  template <class Take>
  CURSIV_NOINLINE bool together(const Ad& ad, const char* src, TextOffset len,
                                TextOffset& far, Take& take, Cursor& c,
                                TextOffset& i) {
    Cursors rows[2], read;
    unsigned now = 0;
    rows[0].push_back(c);
    for (;;) {
      Cursors& cur = rows[now];
      Cursors& next = rows[now ^ 1u];
      while (!_roots.empty() && _roots.begin()->at == i) {
        const Root root = pop();
        join(cur, Cursor{root.state, fresh(i), root.tier});
      }
      if (cur.empty()) return false;
      next.clear();
      read.clear();
      // A cursor an echo of nothing resumes at `i` itself joins the ones this
      // loop still has to read, or, one it has read, what that one goes on to
      // be.
      const auto resume = [&](const Cursor& at) {
        const std::size_t j = join(cur, at);
        if (j < read.size())
          read.begin()[j].tier = std::max(read.begin()[j].tier, at.tier);
      };
      for (std::size_t q = 0; q < cur.size(); ++q) {
        Cursor at = cur.begin()[q];
        at.m.take(ad, at.state, src, i, len, far);
        read.push_back(at);
        for (auto k = ad.relay(at.state); k != kNoRelay;
             k = ad.relay_of(k).next) {
          const LexRelay& r = ad.relay_of(k);
          const auto tier = std::max(r.tier, at.tier);
          resumes(ad, k, src, len, i, [&](TextOffset p) {
            if (p == i)
              resume(Cursor{r.resume, fresh(i), tier});
            else
              push(p, r.resume, tier);
          });
        }
      }
      for (std::size_t q = 0; q < read.size(); ++q) {
        Cursor at = read.begin()[q];
        const std::uint16_t to =
            i < len ? ad.step(at.state, static_cast<unsigned char>(src[i]))
                    : kDeadState;
        if (to == kDeadState) {
          far = std::max(far, i);
          if (at.m.term != kInvalidSymbol)
            take(at.m.term, at.m.end, unsigned{at.tier});
          continue;
        }
        at.state = to;
        join(next, at);
      }
      cur.clear();
      now ^= 1u;
      ++i;
      if (next.size() == 1 && (_roots.empty() || _roots.begin()->at > i)) {
        c = *next.begin();
        return true;
      }
    }
  }

  // Two cursors in one state read the same bytes from here on: the next
  // acceptance speaks for both, so the further match in hand is the only one
  // that can still be the answer, and the best branch among them ranks it.
  static std::size_t join(Cursors& at, const Cursor& c) {
    for (Cursor& o : at) {
      if (o.state != c.state || o.m.term != c.m.term) continue;
      if (c.m.end > o.m.end) o.m = c.m;
      o.tier = std::max(o.tier, c.tier);
      return static_cast<std::size_t>(&o - at.begin());
    }
    at.push_back(c);
    return at.size() - 1u;
  }

  // `_start` reads idle until the first relay is launched.
  static constexpr TextOffset kIdle = ~TextOffset{0};

  SmallRun<Root, 4> _roots;
  TextOffset _start = kIdle;
  bool _sought = false;
  TextOffset _seen = 0;
  // Sorted by relay: one for each relay searched after the walk's first.
  std::unique_ptr<std::vector<Memo>> _memo;
};

}  // namespace cursiv
