#include "cursiv/lsp/text/Position.hpp"

#include <lsp/types.h>

#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "cursiv/core/workspace/TextDocument.hpp"

namespace cursiv {

std::vector<TextDocumentContentChangeEvent> from_lsp(
    std::span<const lsp::TextDocumentContentChangeEvent> changes) {
  std::vector<TextDocumentContentChangeEvent> converted;
  converted.reserve(changes.size());
  for (const lsp::TextDocumentContentChangeEvent& change : changes) {
    if (const auto* whole =
            std::get_if<lsp::TextDocumentContentChangeWholeDocument>(&change)) {
      converted.push_back({std::nullopt, whole->text});
      continue;
    }
    const auto& ranged =
        std::get<lsp::TextDocumentContentChangePartial>(change);
    converted.push_back({from_lsp(ranged.range), ranged.text});
  }
  return converted;
}

}  // namespace cursiv
