// Comparing and printing the one unit every published span is stated in, plus
// the conversion every check makes: a marked pair of offsets is an expectation,
// a provider answers in lines and columns.
#pragma once

#include <lsp/types.h>

#include <string>
#include <utility>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {
struct Document;
}  // namespace cursiv

namespace cursiv::test {

[[nodiscard]] bool isRangeEqual(const lsp::Range& lhs, const lsp::Range& rhs);

[[nodiscard]] std::string rangeToString(const lsp::Range& range);

[[nodiscard]] lsp::Range markedRange(
    const Document& document, const std::pair<TextOffset, TextOffset>& span);

// The check three providers make on a span they answered with, and the sentence
// it fails by.
void expectSameRange(const lsp::Range& actual, const lsp::Range& expected);

}  // namespace cursiv::test
