#include "cursiv/engine/lexer/CharClass.hpp"

#include <utility>

#include "cursiv/text/ClassRanges.hpp"
#include "cursiv/text/unicode/UnicodeProperty.hpp"
#include "cursiv/text/unicode/UnicodeRanges.hpp"

namespace cursiv::engine::gen {

CharClass CharClass::of(std::uint64_t props, bool complemented, std::string_view spec) {
  static const detail::ClassRanges surrogates{{{0xD800, 0xDFFF}}};
  auto chars = detail::ClassRanges::of(spec);
  const bool invalid = chars.holdsInvalid != complemented;
  for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::Count); ++i)
    if ((props >> i) & 1)
      chars.r.insert(chars.r.end(), unicode::kTables[i].begin(), unicode::kTables[i].end());
  if (complemented) chars.r.push_back(surrogates.r.front());
  chars.merge();
  auto scalars = complemented ? chars.complemented() : chars.minus(surrogates);
  return {std::move(scalars.r), invalid};
}

}  // namespace cursiv::engine::gen
