#include "cursiv/ReplacedIndices.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"

namespace cursiv::test {

namespace {

constexpr std::string_view kUnpaired =
    "the expectation's range markers do not pair";

// `kind` names one marker: "index marker" or "range".
template <class Marker>
const Marker* marker(const std::vector<Marker>& markers, std::size_t index,
                     std::string_view kind) {
  if (index < markers.size()) return &markers[index];
  expectedFunction(std::to_string(markers.size()) + " " + std::string(kind) +
                       (markers.size() == 1 ? "" : "s"),
                   std::string(kind) + " " + std::to_string(index),
                   "the expectation's text has no such marker (they count "
                   "from 0)");
  return nullptr;
}

}  // namespace

ReplacedIndices replaceIndices(const ExpectedBase& base) {
  ReplacedIndices replaced;
  const std::string_view markers[] = {base.indexMarker, base.rangeStartMarker,
                                      base.rangeEndMarker};
  std::vector<TextOffset> rangeStack;
  std::string input = base.text;

  while (true) {
    // The index marker outranks the range ones at one position: `<|>` opens
    // with `<|`, and reading it as a range start would swallow every index.
    std::size_t at = std::string::npos;
    int which = -1;
    for (int marker = 0; marker < 3; ++marker) {
      if (markers[marker].empty()) continue;
      const std::size_t found = input.find(markers[marker]);
      if (found == std::string::npos) continue;
      if (found < at || (found == at && marker < which)) {
        at = found;
        which = marker;
      }
    }
    if (which < 0) break;

    const auto offset = static_cast<TextOffset>(at);
    if (which == 0) {
      replaced.indices.push_back(offset);
    } else if (which == 1) {
      rangeStack.push_back(offset);
    } else if (rangeStack.empty()) {
      // A typo in the expectation, never a range from the start of the text.
      expectedFunction("an unopened '" + std::string(base.rangeEndMarker) +
                           "' at offset " + std::to_string(offset),
                       "balanced range markers", kUnpaired);
    } else {
      replaced.ranges.emplace_back(rangeStack.back(), offset);
      rangeStack.pop_back();
    }
    input.erase(at, markers[which].size());
  }
  if (!rangeStack.empty())
    expectedFunction("an unclosed '" + std::string(base.rangeStartMarker) +
                         "' at offset " + std::to_string(rangeStack.back()),
                     "balanced range markers", kUnpaired);

  std::ranges::stable_sort(replaced.ranges, {},
                           &std::pair<TextOffset, TextOffset>::first);
  replaced.output = std::move(input);
  return replaced;
}

const TextOffset* detail::markerAt(const std::vector<TextOffset>& indices,
                                   std::size_t index) {
  return marker(indices, index, "index marker");
}

const std::pair<TextOffset, TextOffset>* detail::markerAt(
    const std::vector<std::pair<TextOffset, TextOffset>>& ranges,
    std::size_t index) {
  return marker(ranges, index, "range");
}

}  // namespace cursiv::test
