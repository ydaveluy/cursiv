// Whether a spelling ends in the word class, so a `kw<>` of it states the
// boundary: its last scalar against the class. A `kw<"(">` states none, or
// `(a` would be refused.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/grammar/unicode/UnicodeProperty.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRanges.hpp"
#include "cursiv/core/text/ClassItem.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/core/utils/ctstring.hpp"

namespace cursiv::detail {

// The spelled scalars of a class, walked as `cr<>` reads them.
constexpr bool spec_holds(std::string_view spec, char32_t cp) {
  bool in = false;
  const ClassFault fault =
      for_class_ranges(spec, [&in, cp](char32_t lo, char32_t hi) {
        in = in || (lo <= cp && cp <= hi);
      });
  return fault == ClassFault::none && in;
}

// The scalar a spelling ends in, `kNoScalar` where its last unit is no whole
// one.
constexpr char32_t last_scalar(std::string_view s) {
  std::size_t at = s.size();
  while (at > 0 && (static_cast<unsigned char>(s[at - 1]) & 0xC0u) == 0x80u)
    --at;
  if (at > 0) --at;
  std::size_t len = 0;
  const char32_t cp = scalar_at(s.data() + at, s.size() - at, len);
  return at + len == s.size() ? cp : kNoScalar;
}

// Whether the class of properties `m` and scalars `spec`, complemented where
// `n`, holds `cp`.
constexpr bool class_holds(std::uint64_t m, bool n, std::string_view spec,
                           char32_t cp) {
  bool in = spec_holds(spec, cp);
  for (unsigned i = 0;
       !in && i < static_cast<unsigned>(unicode::Property::count); ++i)
    if ((m >> i) & 1)
      in = unicode::within(unicode::kTables[i].data(),
                           unicode::kTables[i].size(), cp);
  return in != n;
}

// The case a keyword was typed in has no say: `ikw<"if">` matches `IF` as
// readily as `if`, so under a class not closed under case both cases of the
// last letter are asked. The class and the identifiers' own must agree by
// hand: widening one without the other is a silent language change.
constexpr bool word_final_in(std::string_view s, std::uint64_t m, bool n,
                             std::string_view spec, bool ci) {
  if (s.empty()) return false;
  const char32_t u = last_scalar(s);
  if (u == kNoScalar) return false;
  if (class_holds(m, n, spec, u)) return true;
  if (ci && u < 0x80) {
    const char lo = to_lower(static_cast<char>(u));
    const char hi =
        (lo >= 'a' && lo <= 'z') ? static_cast<char>(lo - 'a' + 'A') : lo;
    return class_holds(m, n, spec, static_cast<unsigned char>(lo)) ||
           class_holds(m, n, spec, static_cast<unsigned char>(hi));
  }
  return false;
}

// The unit a class reads at the end of a spelling, and whether it holds one.
template <class Bound>
struct word_class;
template <std::uint64_t M, bool N, str Sp>
struct word_class<CpSet<M, N, Sp>> {
  static constexpr char32_t last(std::string_view s) { return last_scalar(s); }
  static constexpr bool has(char32_t cp) {
    return class_holds(M, N, Sp.view(), cp);
  }
  static constexpr bool final_in(std::string_view s, bool ci) {
    return word_final_in(s, M, N, Sp.view(), ci);
  }
};

template <str S, class Bound, bool CI = false>
consteval bool word_final_in() {
  return word_class<Bound>::final_in(S.view(), CI);
}

}  // namespace cursiv::detail
