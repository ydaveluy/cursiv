#include "cursiv/ReplacedIndices.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/text/TextOffset.hpp"

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

// Cache each delimiter's next occurrence while the cursor moves forward.
struct Delimiter {
  std::string_view text;
  std::size_t next = 0;
  std::size_t find(std::string_view input, std::size_t from) {
    if (text.empty()) return std::string_view::npos;
    if (next < from || next == 0) next = input.find(text, from);
    return next;
  }
};

}  // namespace

TextOffset ReplacedIndices::singleCaret() const {
  if (indices.size() != 1)
    throw ExpectationError("expected one caret marker, found " +
                           std::to_string(indices.size()));
  return indices.front();
}

ReplacedIndices detail::scan_indices(const ExpectedBase& base) {
  ReplacedIndices replaced;
  Delimiter markers[] = {
      {base.indexMarker}, {base.rangeStartMarker}, {base.rangeEndMarker}};
  std::vector<TextOffset> rangeStack;
  const std::string_view input = base.text;

  // One pass over the text as written, the output built as it goes: the text
  // on each side of a marker never joins into a marker of its own. Each
  // marker's next place is looked for again only once the pass is past it.
  std::size_t from = 0;
  while (true) {
    // The index marker outranks the range ones at one position: `<|>` opens
    // with `<|`, and reading it as a range start would swallow every index.
    std::size_t at = std::string_view::npos;
    int which = -1;
    for (int marker = 0; marker < 3; ++marker) {
      const std::size_t found = markers[marker].find(input, from);
      if (found < at || (found == at && marker < which)) {
        at = found;
        which = marker;
      }
    }
    if (which < 0) break;

    replaced.output.append(input.substr(from, at - from));
    from = at + markers[which].text.size();
    const auto offset = static_cast<TextOffset>(replaced.output.size());
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
  }
  replaced.output.append(input.substr(from));
  if (!rangeStack.empty())
    expectedFunction("an unclosed '" + std::string(base.rangeStartMarker) +
                         "' at offset " + std::to_string(rangeStack.back()),
                     "balanced range markers", kUnpaired);

  return replaced;
}

ReplacedIndices replaceIndices(const ExpectedBase& base) {
  auto replaced = detail::scan_indices(base);
  std::ranges::stable_sort(replaced.ranges, {},
                           &std::pair<TextOffset, TextOffset>::first);
  return replaced;
}

std::string extractMarkers(std::string_view text,
                            std::vector<NamedMarker>& markers,
                            const MarkerSyntax& syntax) {
  if (text.size() > kMaxDocument)
    throw std::overflow_error("fixture text exceeds the document size limit");
  Delimiter range{syntax.rangeStart},
      index{syntax.indexStart.empty() ? syntax.rangeStart : syntax.indexStart};
  Delimiter rangeEnd{syntax.rangeEnd}, indexEnd{syntax.indexEnd};
  if (range.text.empty() || rangeEnd.text.empty() || indexEnd.text.empty() ||
      syntax.delimiter.empty() ||
      (range.text == index.text && rangeEnd.text == indexEnd.text))
    throw ExpectationError("ambiguous or empty marker delimiter");
  markers.clear();
  std::string output;
  output.reserve(text.size());
  std::size_t from = 0;
  while (from < text.size()) {
    const auto r = range.find(text, from), i = index.find(text, from);
    const auto open = std::min(r, i);
    if (open == text.npos) break;
    output.append(text.substr(from, open - from));
    const bool indexOnly =
        i < r || (i == r && index.text.size() > range.text.size());
    const auto prefix = indexOnly ? index.text : range.text;
    from = open + prefix.size();
    const auto re = indexOnly ? text.npos : rangeEnd.find(text, from);
    const auto ie = indexOnly || index.text == range.text
                        ? indexEnd.find(text, from)
                        : text.npos;
    const auto end = std::min(re, ie);
    if (end == text.npos) {
      output += prefix;
      continue;
    }
    const bool caret =
        ie < re || (ie == re && indexEnd.text.size() > rangeEnd.text.size());
    const auto inner = text.substr(from, end - from);
    const auto split = caret ? text.npos : inner.find(syntax.delimiter);
    const auto begin = static_cast<TextOffset>(output.size());
    if (!caret)
      output.append(split == text.npos
                        ? inner
                        : inner.substr(split + syntax.delimiter.size()));
    markers.push_back({std::string(inner.substr(0, split)),
                       {begin, static_cast<TextOffset>(output.size())},
                       caret});
    from = end + (caret ? indexEnd.text.size() : rangeEnd.text.size());
  }
  output.append(text.substr(from));
  return output;
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
