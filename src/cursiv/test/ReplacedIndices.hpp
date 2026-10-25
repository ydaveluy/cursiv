// The markers taken back out of an expectation's text, and where they stood --
// the offsets are into the text the parser is then handed, markers gone.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv::test {

struct ExpectedBase;

struct ReplacedIndices {
  std::string output;
  std::vector<TextOffset> indices;
  // Sorted by start, so a nested pair is reported outermost first.
  std::vector<std::pair<TextOffset, TextOffset>> ranges;
};

[[nodiscard]] ReplacedIndices replaceIndices(const ExpectedBase& base);

}  // namespace cursiv::test
