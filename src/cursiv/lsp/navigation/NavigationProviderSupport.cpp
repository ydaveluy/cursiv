#include "cursiv/lsp/navigation/NavigationProviderSupport.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/core/references/References.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

std::optional<std::vector<lsp::LocationLink>> collect_links_per_declaration(
    const References& references, const Document& document,
    lsp::Position position,
    const std::function<std::vector<lsp::LocationLink>(
        const AstNode&, const CancellationToken&)>& collect,
    const CancellationToken& cancelToken) {
  throw_if_cancelled(cancelToken);
  const Declaration found = references.findDeclaration(
      document, document.textDocument().offsetAt(from_lsp(position)),
      cancelToken);

  std::vector<lsp::LocationLink> links;
  for (const AstNode* const target : found.targets) {
    throw_if_cancelled(cancelToken);
    for (lsp::LocationLink& link : collect(*target, cancelToken))
      links.push_back(std::move(link));
  }
  if (links.empty()) return std::nullopt;
  return links;
}

std::vector<ReferenceDescription> collect_references_per_declaration(
    const References& references, std::span<const AstNode* const> targets,
    function_ref<FindReferencesOptions(const AstNode&)> options,
    const CancellationToken& cancelToken) {
  std::vector<ReferenceDescription> rows;
  for (const AstNode* const target : targets) {
    throw_if_cancelled(cancelToken);
    for (const ReferenceDescription& reference :
         references.findReferences(*target, options(*target), cancelToken)) {
      rows.push_back(reference);
    }
  }
  std::ranges::sort(
      rows, [](const ReferenceDescription& a, const ReferenceDescription& b) {
        if (a.sourceDocumentId != b.sourceDocumentId)
          return a.sourceDocumentId < b.sourceDocumentId;
        if (a.sourceBegin != b.sourceBegin)
          return a.sourceBegin < b.sourceBegin;
        return a.sourceEnd < b.sourceEnd;
      });
  rows.erase(std::unique(rows.begin(), rows.end(),
                         [](const ReferenceDescription& a,
                            const ReferenceDescription& b) {
                           return a.sourceDocumentId == b.sourceDocumentId &&
                                  a.sourceBegin == b.sourceBegin &&
                                  a.sourceEnd == b.sourceEnd;
                         }),
             rows.end());
  return rows;
}

}  // namespace cursiv
