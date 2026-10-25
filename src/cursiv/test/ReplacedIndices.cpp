#include "cursiv/test/ReplacedIndices.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/test/ExpectedBase.hpp"

namespace cursiv::test {

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
    } else {
      const TextOffset begin = rangeStack.empty() ? 0 : rangeStack.back();
      if (!rangeStack.empty()) rangeStack.pop_back();
      replaced.ranges.emplace_back(begin, offset);
    }
    input.erase(at, markers[which].size());
  }

  std::ranges::stable_sort(replaced.ranges, {},
                           &std::pair<TextOffset, TextOffset>::first);
  replaced.output = std::move(input);
  return replaced;
}

}  // namespace cursiv::test
