#pragma once

#include <string_view>

// Whether a candidate answers what the user has typed so far. Its own service
// because a language may want another rule -- and because completion asks it
// once per candidate, which is the only reason it is not a free function.

namespace cursiv {

class FuzzyMatcher {
 public:
  virtual ~FuzzyMatcher() noexcept = default;

  [[nodiscard]] virtual bool match(std::string_view query,
                                   std::string_view text) const = 0;
};

}  // namespace cursiv
