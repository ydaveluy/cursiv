#include "cursiv/test/RangeUtils.hpp"

#include <lsp/types.h>

#include <string>
#include <utility>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/ExpectFunction.hpp"

namespace cursiv::test {

bool isRangeEqual(const lsp::Range& lhs, const lsp::Range& rhs) {
  return lhs.start.character == rhs.start.character &&
         lhs.start.line == rhs.start.line &&
         lhs.end.character == rhs.end.character && lhs.end.line == rhs.end.line;
}

std::string rangeToString(const lsp::Range& range) {
  return std::to_string(range.start.line) + ':' +
         std::to_string(range.start.character) + "--" +
         std::to_string(range.end.line) + ':' +
         std::to_string(range.end.character);
}

lsp::Range markedRange(const Document& document,
                       const std::pair<TextOffset, TextOffset>& span) {
  return to_lsp(document.textDocument().rangeAt(span.first, span.second));
}

void expectSameRange(const lsp::Range& actual, const lsp::Range& expected) {
  expectedFunction(rangeToString(actual), rangeToString(expected),
                   "expected range " + rangeToString(expected) +
                       " does not match actual range " + rangeToString(actual));
}

}  // namespace cursiv::test
