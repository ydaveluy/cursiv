#pragma once

#include <string_view>

#include "cursiv/lsp/completion/FuzzyMatcher.hpp"

// Case-insensitive, and the query's first character must land on a word
// boundary (text start, a camelCase hump, or just after `_`); the rest is a
// subsequence. A plain subsequence would let `ram` answer `Grammar`; anchoring
// the first character is what keeps `GK` -> `GrammarKeyword` useful instead.

namespace cursiv {

class DefaultFuzzyMatcher : public FuzzyMatcher {
 public:
  [[nodiscard]] bool match(std::string_view query,
                           std::string_view text) const override;

 protected:
  // A hump (`aB`) or the character after an underscore.
  [[nodiscard]] virtual bool isWordTransition(char16_t previous,
                                              char16_t current) const noexcept;
};

}  // namespace cursiv
