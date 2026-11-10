// The markers taken back out of an expectation's text, and where they were --
// the offsets are into the text the parser is then handed, markers gone. A
// range end with no open range, or a range left open, fails the expectation.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/text/TextOffset.hpp"

namespace cursiv::test {

struct ExpectedBase;

struct ReplacedIndices {
  std::string output;
  std::vector<TextOffset> indices;
  // Sorted by start, so a pair opened inside another comes after it; pairs
  // that open at one offset keep the order they close in, innermost first.
  std::vector<std::pair<TextOffset, TextOffset>> ranges;
};

[[nodiscard]] ReplacedIndices replaceIndices(const ExpectedBase& base);

namespace detail {

// Marker `index` of a text, counted from 0. A text with fewer markers fails
// the expectation, saying which marker was asked for and how many there are,
// and answers null.
[[nodiscard]] const TextOffset* markerAt(const std::vector<TextOffset>& indices,
                                         std::size_t index);
[[nodiscard]] const std::pair<TextOffset, TextOffset>* markerAt(
    const std::vector<std::pair<TextOffset, TextOffset>>& ranges,
    std::size_t index);

}  // namespace detail

}  // namespace cursiv::test
