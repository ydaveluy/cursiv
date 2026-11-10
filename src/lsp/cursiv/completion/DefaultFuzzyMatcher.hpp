#pragma once

#include <string_view>

#include "cursiv/completion/FuzzyMatcher.hpp"

// Case-insensitive subsequence whose first character falls on a word boundary
// (text start, a camelCase capital, or after `_`): `GK` finds `GrammarKeyword`.

namespace cursiv {

class DefaultFuzzyMatcher : public FuzzyMatcher {
 public:
  [[nodiscard]] bool match(std::string_view query,
                           std::string_view text) const override;

 protected:
  // A camelCase capital (`aB`) or the character after an underscore.
  [[nodiscard]] virtual bool isWordTransition(char16_t previous,
                                              char16_t current) const noexcept;
};

}  // namespace cursiv
