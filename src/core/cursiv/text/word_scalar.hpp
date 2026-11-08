// Whether a scalar is one a word is made of: a letter (`\p{L}`), an ASCII
// digit or `_`. What a caret completes and a selection widens over.
#pragma once

#include <iterator>

#include "cursiv/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/grammar/unicode/UnicodeRanges.hpp"

namespace cursiv::detail {

[[nodiscard]] inline bool word_scalar(char32_t scalar) noexcept {
  return (scalar >= '0' && scalar <= '9') || scalar == '_' ||
         unicode::detail::within(unicode::detail::kL,
                                 std::size(unicode::detail::kL), scalar);
}

}  // namespace cursiv::detail
