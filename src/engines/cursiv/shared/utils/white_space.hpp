// Whether a text is only Unicode `White_Space`, which an editor never offers as
// a word. Which text is a blank is still the skipper's choice.
#pragma once

#include <iterator>
#include <string_view>

#include "cursiv/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/grammar/unicode/UnicodeRanges.hpp"
#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

[[nodiscard]] constexpr bool white_space_only(std::string_view text) noexcept {
  for (std::size_t at = 0, len = 0; at < text.size(); at += len)
    if (!unicode::within(unicode::kWhite_Space,
                         std::size(unicode::kWhite_Space),
                         scalar_at(text.data() + at, text.size() - at, len)))
      return false;
  return true;
}

}  // namespace cursiv::engine
