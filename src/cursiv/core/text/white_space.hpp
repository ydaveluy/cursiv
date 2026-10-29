// Whether a text is white space alone, as Unicode's `White_Space` says:
// layout, which an editor never offers as a word. Which text is a blank stays
// the grammar's skipper's to say.
#pragma once

#include <iterator>
#include <string_view>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRanges.hpp"
#include "cursiv/core/text/scalar_at.hpp"

namespace cursiv {

[[nodiscard]] constexpr bool white_space_only(std::string_view text) noexcept {
  for (std::size_t at = 0, len = 0; at < text.size(); at += len)
    if (!unicode::within(unicode::kWhite_Space,
                         std::size(unicode::kWhite_Space),
                         scalar_at(text.data() + at, text.size() - at, len)))
      return false;
  return true;
}

}  // namespace cursiv
