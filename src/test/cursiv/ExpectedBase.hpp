// The text a check is written against, with its positions marked inside:
// `<|>` an index, `<|`...`|>` a range. Input structs repeat these fields, so
// they keep designated initializers.
#pragma once

#include <string>
#include <string_view>
#include <variant>

#include "cursiv/ParseHelperOptions.hpp"

namespace cursiv::test {

struct ExpectedBase {
  std::string text{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  // Whether the document is dropped as soon as the check has run.
  bool disposeAfterCheck = false;
};

namespace detail {

// The fields an input struct repeats, read off it, or off the arm a variant
// of input structs holds.
template <class Input>
[[nodiscard]] ExpectedBase baseOf(const Input& input) {
  return ExpectedBase{input.text,           input.parseOptions,
                      input.indexMarker,    input.rangeStartMarker,
                      input.rangeEndMarker, input.disposeAfterCheck};
}
template <class... Inputs>
[[nodiscard]] ExpectedBase baseOf(const std::variant<Inputs...>& input) {
  return std::visit([](const auto& arm) { return baseOf(arm); }, input);
}

}  // namespace detail

}  // namespace cursiv::test
