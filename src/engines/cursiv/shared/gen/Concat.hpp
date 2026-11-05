// Generated code, piece by piece: each value stands where it goes, and the
// braces of the code stay as written.
#pragma once

#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

namespace cursiv {

template <class... Parts>
[[nodiscard]] std::string concat(const Parts&... parts) {
  std::string out;
  const auto add = [&out]<class Part>(const Part& part) {
    if constexpr (std::is_convertible_v<const Part&, std::string_view>)
      out += std::string_view(part);
    else
      std::format_to(std::back_inserter(out), "{}", part);
  };
  (add(parts), ...);
  return out;
}

}  // namespace cursiv
