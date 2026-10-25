// Byte and scalar frontiers use one view of either engine's frozen classes.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Utf8.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/lex_scalar_in.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LexBoundary {
  const ByteSet* sets = nullptr;
  const std::uint16_t* decode = nullptr;
  const ByteSet* wordBytes = nullptr;
  const unicode::Range* wordRanges = nullptr;
  std::size_t nwordRanges = 0;
  const std::uint32_t* setScalars = nullptr;
  const unicode::Range* scalarRanges = nullptr;

  bool word_at(const char* src, TextOffset i, TextOffset len) const {
    const auto b = static_cast<unsigned char>(src[i]);
    if (wordBytes->has(b)) return true;
    return nwordRanges != 0 && b >= 0xC2 &&
           lex_scalar_in(wordRanges, nwordRanges, src + i, len - i);
  }

  CURSIV_ALWAYS_INLINE bool at(std::uint16_t set, const char* src, TextOffset i,
                               TextOffset len) const {
    const auto b = static_cast<unsigned char>(src[i]);
    if (sets[set].has(b)) return true;
    if (nwordRanges != 0 || setScalars != nullptr) {
      const auto d = decode[set];
      if (d != kNoSet && sets[d].has(b)) {
        if (setScalars != nullptr)
          if (const auto ranges = setScalars[set]; ranges != 0) {
            const unicode::Range* const r = scalarRanges + (ranges >> 16);
            const std::size_t n = ranges & 0xFFFFu;
            return (!lex_lists_only_back(r, n) &&
                    lex_scalar_in(r, n, src + i, len - i)) ||
                   lex_scalar_begun(r, n, src, i, len);
          }
        return lex_scalar_in(wordRanges, nwordRanges, src + i, len - i);
      }
    }
    return false;
  }

  TextOffset reach(std::uint16_t set, const char* src, TextOffset i,
                   TextOffset len) const {
    if (nwordRanges != 0 || setScalars != nullptr) {
      const auto b = static_cast<unsigned char>(src[i]);
      const auto d = decode[set];
      if (d != kNoSet && sets[d].has(b)) {
        // A continuation byte decodes for a bound that looks back.
        const auto width =
            static_cast<TextOffset>(b < 0xC0 ? 3 : utf8_width(src[i]));
        return width < len - i ? i + width : len;
      }
    }
    return i + 1;
  }
};

// The frontiers of a frozen table: an LL table holds pointers and counts, an
// LR configuration arrays.
template <class Cfg>
constexpr LexBoundary lex_boundary(const Cfg& c) {
  if constexpr (requires { c.nwordRanges; })
    return {c.sets,        c.setDecode,  &c.wordBytes,  c.wordRanges,
            c.nwordRanges, c.setScalars, c.scalarRanges};
  else
    return {c.sets.data(),
            c.setDecode.data(),
            &c.wordBytes,
            c.wordRanges.data(),
            c.wordRanges.size(),
            c.scalarRanges.size() != 0 ? c.setScalars.data() : nullptr,
            c.scalarRanges.data()};
}

}  // namespace cursiv
