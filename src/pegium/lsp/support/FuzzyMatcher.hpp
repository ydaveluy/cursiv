#pragma once

#include <string_view>

namespace pegium {

/// Matches free-text queries against candidate strings.
class FuzzyMatcher {
public:
  virtual ~FuzzyMatcher() noexcept = default;

  /// Returns whether `text` matches `query`.
  [[nodiscard]] virtual bool
  match(std::string_view query, std::string_view text) const = 0;
};

} // namespace pegium
