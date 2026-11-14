#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/text/unicode/UnicodeProperty.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/text/ClassItem.hpp"
#include "cursiv/text/ClassRanges.hpp"
#include "cursiv/utils/ctstring.hpp"

// Character classes as the generators read and name them.

namespace cursiv::engine {

inline void malformed_cr_spec() { /* the call is the diagnostic */ }

// The bytes a spelling of ASCII characters names. It takes the text, not the
// `str` type, so every spelling shares one instantiation.
constexpr CharSet parse_charset(std::string_view Spec) {
  CharSet cs{};
  const ClassFault fault =
      for_class_ranges(Spec, [&cs](char32_t lo, char32_t hi) {
        if (hi > 0x7F) malformed_cr_spec();
        for (char32_t v = lo; v <= hi; ++v)
          cs.set(static_cast<unsigned char>(v));
      });
  if (fault != ClassFault::none) malformed_cr_spec();
  return cs;
}

// Whether a spelling names ASCII scalars only.
constexpr bool cp_ascii_of(std::string_view spec) {
  const ClassRanges r = ClassRanges::of(spec);
  return r.r.empty() || r.r.back().hi < 0x80;
}

// What a class is called, for a dump and a terminal's name: `\p{L|Nd}`,
// `\p{^L}`, the spelled scalars appended. One body both measures and writes,
// so the reserved length matches the text.
template <class Sink>
constexpr void cp_name_emit(std::uint64_t props, bool complement,
                            std::string_view scalars, Sink sink) {
  const auto put = [&sink](std::string_view v) {
    for (const char ch : v) sink(ch);
  };
  // Whitespace goes by its short name.
  if (props == 0 &&
      scalars ==
          R"(\x09-\x0d \xa0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000\ufeff)") {
    put(complement ? "\\S" : "\\s");
    return;
  }
  put("\\p{");
  if (complement) sink('^');
  bool first = true;
  for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::Count); ++i)
    if ((props >> i) & 1) {
      if (!first) sink('|');
      first = false;
      put(unicode::detail::kNames[i]);
    }
  put(scalars);
  sink('}');
}

}  // namespace cursiv::engine
