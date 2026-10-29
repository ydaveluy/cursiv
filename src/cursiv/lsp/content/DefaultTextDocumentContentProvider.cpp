#include "cursiv/lsp/content/DefaultTextDocumentContentProvider.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/CaseFold.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "lsp/types.h"

namespace cursiv {

DefaultTextDocumentContentProvider::DefaultTextDocumentContentProvider(
    const SharedCoreServices& shared, std::vector<std::string> schemes)
    : _shared(shared), _schemes(std::move(schemes)) {}

std::span<const std::string> DefaultTextDocumentContentProvider::schemes()
    const noexcept {
  return _schemes;
}

std::optional<lsp::TextDocumentContentResult>
DefaultTextDocumentContentProvider::provideTextDocumentContent(
    const lsp::TextDocumentContentParams& params) const {
  // A scheme has no case, and the URI type lowers the one the client sent.
  if (const auto scheme = params.uri.scheme();
      scheme.empty() || std::ranges::none_of(_schemes, [&](const auto& own) {
        return folded_equal(own, scheme);
      }))
    return std::nullopt;
  const std::shared_ptr<Document> document =
      _shared.documents->getDocument(params.uri.toString());
  if (document == nullptr) return std::nullopt;
  return lsp::TextDocumentContentResult{
      std::string(document->textDocument().getText())};
}

}  // namespace cursiv
