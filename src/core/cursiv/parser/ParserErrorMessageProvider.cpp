#include "cursiv/parser/ParserErrorMessageProvider.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"

namespace cursiv {

// What the parser itself said wins: a conversion error carries its own
// sentence, and only a repair is left to be worded here.
std::string ParserErrorMessageProvider::buildMessage(
    const Grammar& grammar, const ParseDiagnostic& diagnostic,
    std::string_view text) const {
  if (!diagnostic.message.empty()) return diagnostic.message;
  switch (diagnostic.kind) {
    case DiagnosticKind::inserted:
      return buildInsertedMessage(grammar, diagnostic, text);
    case DiagnosticKind::deleted:
      return buildDeletedMessage(grammar, diagnostic, text);
    case DiagnosticKind::replaced:
      return buildMismatchTokenMessage(grammar, diagnostic, text);
    case DiagnosticKind::incomplete:
      return buildIncompleteMessage(grammar, diagnostic, text);
    case DiagnosticKind::bounded:
      return buildBoundedMessage(grammar, diagnostic, text);
    case DiagnosticKind::conversion_error:
      break;
  }
  return "Syntax error";
}

// What the grammar accepts where a repair was owed. A repair is owed
// terminals, so their spellings are asked of the grammar directly -- no
// descent through groups and repetitions, and no cycle to guard against.
// The first names the sentence: where it has no spelling of its own the
// others say nothing about it, and its name alone is the better word.
std::vector<std::string_view> expected_spellings(const Grammar& grammar,
                                                 const ParseDiagnostic& d,
                                                 std::size_t limit) {
  std::vector<std::string_view> spellings;
  const auto add = [&](ElementId owed) {
    if (owed == kNoElement) return;
    for (const std::string_view text : grammar.terminalSpellings(owed))
      if (spellings.size() < limit &&
          std::ranges::find(spellings, text) == spellings.end())
        spellings.push_back(text);
  };
  if (limit == 0) return spellings;
  add(d.element);
  if (spellings.empty()) return spellings;
  for (const ElementId owed : d.also) add(owed);
  return spellings;
}

}  // namespace cursiv
