#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace cursiv {

// Transparent, so a lookup by `string_view` does not materialise a
// `std::string` first.
struct TransparentStringHash {
  using is_transparent = void;
  [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};

}  // namespace cursiv
