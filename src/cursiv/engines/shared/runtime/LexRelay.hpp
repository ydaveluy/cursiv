// The rest of a token past its echo, read by a search: the run the mark
// chose is read back off the opener, the echo is looked for -- as a closer,
// or right where the walk stopped -- and the walk resumes past it. A nested
// run counts its openers instead, and ends at the closer at depth zero.
#pragma once

#include <cstdint>
#include <cstring>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/LexMarkRun.hpp"
#include "cursiv/engines/shared/runtime/LexNestedSearch.hpp"
#include "cursiv/engines/shared/runtime/lex_find.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// A closer that holds a run of `pad` between `before` and the echo: the first
// echo and `after` behind such a run and a `before`. The echo starts where the
// run ends, which the generator checked: the runs behind two echoes stand
// apart, and each is read once.
inline TextOffset lex_echo_padded(const LexRelay& r, const ByteSet& pad,
                                  const char* names, const char* src,
                                  TextOffset len, TextOffset at,
                                  TextOffset markBegin, TextOffset n) {
  // The room counted in 64 bits: a run near the 4 GiB bound plus `after`
  // does not fit in an offset.
  if (len - at < std::uint64_t{r.beforeLen} + n + r.afterLen) return 0;
  const TextOffset from = at + r.beforeLen;
  const char* const before = names + r.beforeOff;
  const char* const hit = lex_find_closer(
      {src + from, len - from}, {}, {src + markBegin, n},
      {names + r.afterOff, r.afterLen}, [&](const char* echo) {
        const auto e = static_cast<TextOffset>(echo - src);
        auto q = e;
        while (q > from && pad.has(static_cast<unsigned char>(src[q - 1]))) --q;
        for (; q <= e; ++q)
          if (std::memcmp(src + q - r.beforeLen, before, r.beforeLen) == 0)
            return true;
        return false;
      });
  return hit == nullptr ? 0 : static_cast<TextOffset>(hit - src) + n + r.afterLen;
}

// Where the rest of the token starts, for a mark whose run is
// `[markBegin, at - fence)`: past its echo, or zero where the echo does not
// stand. `at` is where the walk stopped.
CURSIV_ALWAYS_INLINE inline TextOffset lex_echo_seek(
    const LexRelay& r, const ByteSet* sets, const char* names,
    const char* src, TextOffset len, TextOffset at, TextOffset markBegin) {
  const auto n = at - r.fence - markBegin;
  if (r.padSet != kNoSet)
    return lex_echo_padded(r, sets[r.padSet], names, src, len, at, markBegin,
                           n);
  if (!r.closes) {
    if (len - at < n ||
        (n != 0 && std::memcmp(src + at, src + markBegin, n) != 0))
      return 0;
    return at + n;
  }
  const char* const hit = lex_find_closer(
      {src + at, len - at}, {names + r.beforeOff, r.beforeLen},
      {src + markBegin, n}, {names + r.afterOff, r.afterLen},
      [](const char*) { return true; });
  if (hit == nullptr) return 0;
  return static_cast<TextOffset>(hit - src) + r.beforeLen + n + r.afterLen;
}

// Where the rest of the token starts, past the echo, or zero where the echo
// does not stand. `start` is the token's first byte, `at` where the walk
// stopped: the fence and the run stand right before it.
CURSIV_ALWAYS_INLINE inline TextOffset lex_relay_seek(
    const LexRelay& r, const ByteSet* sets, const char* names, const char* src,
    TextOffset len, TextOffset start, TextOffset at) {
  if (r.nested) return LexNestedSearch::once(r, names, src, len, at - r.back);
  if (at < start || at - start < r.fence) return 0;
  return lex_echo_seek(
      r, sets, names, src, len, at,
      LexMarkRun::back(sets[r.markSet], src, start, at - r.fence));
}

// Where the token ends, or zero where it does not stand here: the seek, and
// `walk(k, resume, p)` reading the rest from `p` in the relay's resume state.
template <class Walk>
inline TextOffset lex_relay(std::uint16_t k, const LexRelay& r,
                            const ByteSet* sets, const char* names,
                            const char* src, TextOffset len, TextOffset start,
                            TextOffset at, Walk walk) {
  const TextOffset p = lex_relay_seek(r, sets, names, src, len, start, at);
  if (p == 0) return 0;
  const auto s = walk(k, r.resume, p);
  return s.term == static_cast<SymbolId>(r.term) ? s.end : 0;
}

}  // namespace cursiv
