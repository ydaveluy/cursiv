#include "cursiv/RangeUtils.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/navigation/DeclarationProvider.hpp"
#include "cursiv/navigation/DefinitionProvider.hpp"
#include "cursiv/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/navigation/ImplementationProvider.hpp"
#include "cursiv/navigation/ReferencesProvider.hpp"
#include "cursiv/navigation/TypeDefinitionProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

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

lsp::Range detail::markedRange(const Document& document,
                               const std::pair<TextOffset, TextOffset>& span) {
  return toLsp(document.textDocument().rangeAt(span.first, span.second));
}

void detail::sort_locations(std::vector<lsp::Location>& locations) {
  std::ranges::sort(locations,
                    [](const lsp::Location& a, const lsp::Location& b) {
                      if (a.uri != b.uri) return a.uri < b.uri;
                      const auto key = [](const auto& r) {
                        return std::pair{fromLsp(r.start), fromLsp(r.end)};
                      };
                      return key(a.range) < key(b.range);
                    });
}

void detail::check_navigation(const LspServices& services,
                              const Document& document, TextOffset caret,
                              NavigationQuery query,
                              std::span<const lsp::Location> expected,
                              const CancellationToken& cancel, bool ordered,
                              bool fullRange) {
  const auto at = toLsp(document.textDocument().positionAt(caret));
  std::vector<lsp::Location> actual;
  const auto targets = [&](const auto& provider, auto request,
                           const char* missing) {
    if (!provider) throw ExpectationError(missing);
    if (auto links = ((*provider).*request)(document, at, cancel)) {
      actual.reserve(links->size());
      for (auto& link : *links)
        actual.push_back(
            {std::move(link.targetUri),
             fullRange ? link.targetRange : link.targetSelectionRange});
    }
  };
  switch (query) {
    case NavigationQuery::Definition:
      targets(services.definitionProvider, &DefinitionProvider::getDefinition,
              "missing definition provider");
      break;
    case NavigationQuery::Declaration:
      targets(services.declarationProvider,
              &DeclarationProvider::getDeclaration,
              "missing declaration provider");
      break;
    case NavigationQuery::Implementation:
      targets(services.implementationProvider,
              &ImplementationProvider::getImplementation,
              "missing implementation provider");
      break;
    case NavigationQuery::TypeDefinition:
      targets(services.typeDefinitionProvider,
              &TypeDefinitionProvider::getTypeDefinition,
              "missing type definition provider");
      break;
    case NavigationQuery::Highlights: {
      if (!services.documentHighlightProvider)
        throw ExpectationError("missing highlight provider");
      const auto uri = lsp::Uri::parse(document.uri);
      for (auto& hit : services.documentHighlightProvider->getDocumentHighlight(
               document, at, cancel))
        actual.push_back({uri, hit.range});
      break;
    }
    case NavigationQuery::References:
    case NavigationQuery::ReferencesWithDeclaration:
      if (!services.referencesProvider)
        throw ExpectationError("missing references provider");
      actual = services.referencesProvider->findReferences(
          document, at, query == NavigationQuery::ReferencesWithDeclaration,
          cancel);
      break;
  }
  if (!ordered) sort_locations(actual);
  expectedFunction(std::to_string(actual.size()),
                   std::to_string(expected.size()),
                   "number of navigation targets");
  for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
    expectedFunction(actual[i].uri.toString(), expected[i].uri.toString(),
                     "target URI");
    expectSameRange(actual[i].range, expected[i].range);
  }
}

void detail::expectSameRange(const lsp::Range& actual,
                             const lsp::Range& expected) {
  expectedFunction(rangeToString(actual), rangeToString(expected), "range");
}

std::vector<lsp::TextEdit> detail::editsFor(const lsp::WorkspaceEdit& edit,
                                            std::string_view uri) {
  std::vector<lsp::TextEdit> edits;
  if (edit.documentChanges.has_value()) {
    for (const auto& change : *edit.documentChanges) {
      const auto* const text = std::get_if<lsp::TextDocumentEdit>(&change);
      if (text == nullptr || text->textDocument.uri.toString() != uri) continue;
      for (const auto& one : text->edits)
        if (auto* plain = plain_edit(one)) edits.push_back(*plain);
    }
  } else if (edit.changes.has_value()) {
    for (const auto& [key, filed] : *edit.changes)
      if (key.toString() == uri)
        edits.insert(edits.end(), filed.begin(), filed.end());
  }
  std::ranges::stable_sort(edits, {}, [](const lsp::TextEdit& e) {
    return std::pair{e.range.start.line, e.range.start.character};
  });
  return edits;
}

}  // namespace cursiv::test
