// Comparing and printing the one unit every published span is stated in, and
// the conversions the checks make: a marked pair of offsets is an expectation,
// a provider answers in lines and columns, an edit is filed by document.
#pragma once

#include <lsp/types.h>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"

namespace cursiv {
struct Document;
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

[[nodiscard]] bool isRangeEqual(const lsp::Range& lhs, const lsp::Range& rhs);

[[nodiscard]] std::string rangeToString(const lsp::Range& range);

namespace detail {

[[nodiscard]] lsp::Range markedRange(
    const Document& document, const std::pair<TextOffset, TextOffset>& span);

enum class NavigationQuery {
  Definition,
  Declaration,
  Implementation,
  TypeDefinition,
  References,
  ReferencesWithDeclaration,
  Highlights
};

void sort_locations(std::vector<lsp::Location>& locations);
// The expected locations are sorted once for every queried caret.
void check_navigation(const LspServices& services, const Document& document,
                      TextOffset caret, NavigationQuery query,
                      std::span<const lsp::Location> expected,
                      const CancellationToken& cancel = kNoCancel,
                      bool ordered = false, bool fullRange = false);

// The check three providers make on a span they answered with, and the sentence
// it fails by.
void expectSameRange(const lsp::Range& actual, const lsp::Range& expected);

inline const lsp::TextEdit* plain_edit(const lsp::TextEdit& edit) {
  return &edit;
}
inline const lsp::TextEdit* plain_edit(
    const decltype(lsp::TextDocumentEdit::edits)::value_type& edit) {
  if (auto* plain = std::get_if<lsp::TextEdit>(&edit)) return plain;
  return std::get_if<lsp::AnnotatedTextEdit>(&edit);
}

// The text edits `edit` files under `uri`, sorted by position: from its
// `documentChanges` when it has them, as a client that reads them does.
[[nodiscard]] std::vector<lsp::TextEdit> editsFor(
    const lsp::WorkspaceEdit& edit, std::string_view uri);

}  // namespace detail

}  // namespace cursiv::test
