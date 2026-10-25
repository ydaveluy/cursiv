// The text a check is written against, with the positions it cares about
// spelled inside it: `<|>` marks an index, `<|`...`|>` a range. Every input
// struct repeats these fields rather than deriving: an aggregate with a base
// loses designated initializers, which is how a check is written.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/test/ParseHelperOptions.hpp"

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

}  // namespace cursiv::test
