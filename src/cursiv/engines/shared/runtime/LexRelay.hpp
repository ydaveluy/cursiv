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

// A closer that holds a run of `pad` between `before` and the echo: the
// first `before` whose run the echo and `after` follow. The run is read to
// its end, which the generator checked is where the echo starts; a run is
// read once however many `before`s stand in it.
inline TextOffset lex_echo_padded(const LexRelay& r, const ByteSet& pad,
                                  const char* names, const char* src,
                                  TextOffset len, TextOffset at,
                                  TextOffset markBegin, TextOffset n) {
  const char* const before = names + r.beforeOff;
  const char* const after = names + r.afterOff;
  TextOffset runFrom = 0, runEnd = 0, tried = 0;
  for (TextOffset p = at; p < len;) {
    const char* hit = lex_find(src + p, len - p, before, r.beforeLen);
    if (hit == nullptr) return 0;
    const TextOffset q = static_cast<TextOffset>(hit - src) + r.beforeLen;
    p = static_cast<TextOffset>(hit - src) + 1;
    if (q < runFrom || q >= runEnd) {
      runFrom = q;
      runEnd = q;
      while (runEnd < len && pad.has(static_cast<unsigned char>(src[runEnd])))
        ++runEnd;
    }
    if (runEnd == tried) continue;
    tried = runEnd;
    const TextOffset e = runEnd;
    if (len - e < n + r.afterLen) continue;
    TextOffset k = 0;
    while (k < n && src[e + k] == src[markBegin + k]) ++k;
    if (k == n && std::memcmp(src + e + n, after, r.afterLen) == 0)
      return e + n + r.afterLen;
  }
  return 0;
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
  const std::uint64_t size = std::uint64_t{r.beforeLen} + n + r.afterLen;
  if (size > len - at) return 0;
  const auto m = static_cast<TextOffset>(size);
  char closer[128];
  const char* hit;
  if (size <= sizeof closer) {
    std::memcpy(closer, names + r.beforeOff, r.beforeLen);
    std::memcpy(closer + r.beforeLen, src + markBegin, n);
    std::memcpy(closer + r.beforeLen + n, names + r.afterOff, r.afterLen);
    hit = lex_find(src + at, len - at, closer, m);
  } else {
    hit =
        lex_find_long({src + at, len - at}, {names + r.beforeOff, r.beforeLen},
                      {src + markBegin, n}, {names + r.afterOff, r.afterLen});
  }
  if (hit == nullptr) return 0;
  return static_cast<TextOffset>(hit - src) + m;
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
