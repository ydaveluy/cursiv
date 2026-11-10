#pragma once

#include <string_view>

// Whether a candidate matches what the user has typed so far. A service, so a
// language can replace the rule.

namespace cursiv {

class FuzzyMatcher {
 public:
  virtual ~FuzzyMatcher() noexcept = default;

  [[nodiscard]] virtual bool match(std::string_view query,
                                   std::string_view text) const = 0;
};

}  // namespace cursiv
