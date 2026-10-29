// The search that ends a balanced run: from right after its opener, the closer
// that brings the depth back to zero. A group closes where it closes whoever
// reads it, so an instance keeps every group it settled and a walk that meets
// an opener at each byte reads the text once, not once per opener.
#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/table/FlatMap.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"

namespace cursiv {

class LexNestedSearch {
 public:
  // Where the run opened right before `from` ends, past its closer, or zero;
  // where no closer balances it and `eof` says so, the end of the text.
  CURSIV_ALWAYS_INLINE static TextOffset once(const LexRelay& r,
                                              const char* names,
                                              const char* src, TextOffset len,
                                              TextOffset from) {
    const TextOffset end =
        r.several ? scan_many<false>(nullptr, r, names, src, len, from)
                  : scan<false, false>(nullptr, r, names, src, len, from);
    return end == 0 && r.eof ? len : end;
  }

  // The same, and every group settled on the way answers the searches after.
  CURSIV_NOINLINE TextOffset seek(const LexRelay& r, const char* names,
                                  const char* src, TextOffset len,
                                  TextOffset from) {
    TextOffset end;
    if (from >= _dry)
      end = 0;
    else if (const TextOffset* known = find(from))
      end = *known;
    else
      end = r.several ? scan_many<true>(this, r, names, src, len, from)
                      : scan<true, false>(this, r, names, src, len, from);
    return end == 0 && r.eof ? len : end;
  }

 private:
  static constexpr TextOffset kFree = ~TextOffset{0};

  struct Lit {
    const char* text;
    TextOffset n;
    bool close;
    const char* at;
  };

  // One pass: where each literal's first byte next stands is found once and
  // kept until the cursor passes it. The earlier start wins, and an opener
  // starting before a closer deepens even where the two overlap -- `/*/`
  // deepens before `*/` closes; a closer wins a tie. `Many`: several literals
  // a side, the nearest of a side standing for it, and of two a side spells at
  // one byte, the longer read.
  template <bool Remember, bool Many>
  CURSIV_ALWAYS_INLINE static TextOffset scan(LexNestedSearch* self,
                                              const LexRelay& r,
                                              const char* names,
                                              const char* src, TextOffset len,
                                              TextOffset from) {
    const char* const open = names + r.beforeOff;
    const char* const close = names + r.afterOff;
    const char* const stop = src + len;
    const auto next = [&](const char* q, char b) -> const char* {
      return q < stop ? static_cast<const char*>(std::memchr(
                            q, b, static_cast<std::size_t>(stop - q)))
                      : nullptr;
    };
    const auto spells = [&](const char* q, const char* lit, TextOffset n) {
      return static_cast<TextOffset>(stop - q) >= n &&
             std::memcmp(q, lit, n) == 0;
    };
    [[maybe_unused]] std::vector<Lit> lits;
    if constexpr (Many) {
      const auto list = [&](TextOffset off, TextOffset size, bool close) {
        for (TextOffset i = 0; i < size;
             i += 1u + static_cast<unsigned char>(names[off + i]))
          lits.push_back(Lit{names + off + i + 1,
                             static_cast<unsigned char>(names[off + i]), close,
                             nullptr});
      };
      list(r.beforeOff, r.beforeLen, false);
      list(r.afterOff, r.afterLen, true);
    }
    const auto length = [&](const char* at, bool closer) -> TextOffset {
      if constexpr (Many) {
        TextOffset best = 0;
        for (const Lit& l : lits)
          if (l.close == closer && l.n > best && spells(at, l.text, l.n))
            best = l.n;
        return best;
      } else {
        const TextOffset n = closer ? r.afterLen : r.beforeLen;
        return spells(at, closer ? close : open, n) ? n : 0;
      }
    };
    if constexpr (Remember) self->_open.assign(1, from);
    const char* q = src + from;
    const char* o = nullptr;
    const char* c = nullptr;
    if constexpr (Many) {
      for (Lit& l : lits) l.at = next(q, l.text[0]);
    } else {
      o = next(q, open[0]);
      c = next(q, close[0]);
    }
    for (unsigned depth = 1;;) {
      if constexpr (Many) {
        o = c = nullptr;
        for (Lit& l : lits) {
          if (l.at != nullptr && l.at < q) l.at = next(q, l.text[0]);
          const char*& side = l.close ? c : o;
          if (l.at != nullptr && (side == nullptr || l.at < side)) side = l.at;
        }
      } else {
        if (o != nullptr && o < q) o = next(q, open[0]);
        if (c != nullptr && c < q) c = next(q, close[0]);
      }
      if (c == nullptr) {
        if constexpr (Remember) self->no_closer_from(q - src);
        return 0;
      }
      const bool first = o != nullptr && o < c;
      TextOffset n = 0;
      if (!first && (n = length(c, true)) != 0) {
        const TextOffset end = static_cast<TextOffset>(c - src) + n;
        if constexpr (Remember) {
          self->put(self->_open.back(), end);
          self->_open.pop_back();
        }
        if (--depth == 0) return end;
        q = c + n;
      } else if ((first || o == c) && (n = length(o, false)) != 0) {
        const TextOffset b = static_cast<TextOffset>(o - src) + n;
        q = o + n;
        if constexpr (Remember) {
          if (const TextOffset* known = self->find(b)) {
            if (*known == 0) {
              self->give_up();
              return 0;
            }
            q = src + *known;
            continue;
          }
          self->_open.push_back(b);
        }
        ++depth;
      } else {
        q = (first ? o : c) + 1;
      }
    }
  }

  template <bool Remember>
  CURSIV_NOINLINE static TextOffset scan_many(LexNestedSearch* self,
                                              const LexRelay& r,
                                              const char* names,
                                              const char* src, TextOffset len,
                                              TextOffset from) {
    return scan<Remember, true>(self, r, names, src, len, from);
  }

  // No closer's first byte stands from `q` on: every group still open is
  // lost, and so is any search that starts there.
  void no_closer_from(std::size_t q) {
    if (q < _dry) _dry = static_cast<TextOffset>(q);
    give_up();
  }

  void give_up() {
    for (const TextOffset b : _open) put(b, 0);
    _open.clear();
  }

  // Past the closer of the group opened at `key`; zero where it has none.
  const TextOffset* find(TextOffset key) {
    return key == kFree ? nullptr : _ends.find(key);
  }
  void put(TextOffset key, TextOffset end) {
    if (key != kFree) _ends.emplace(key).first = end;
  }

  FlatMap<TextOffset> _ends;
  // The groups the search in hand has opened and not closed, by boundary.
  std::vector<TextOffset> _open;
  TextOffset _dry = kFree;
};

}  // namespace cursiv
