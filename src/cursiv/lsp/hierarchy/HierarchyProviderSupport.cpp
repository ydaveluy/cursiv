#include "cursiv/lsp/hierarchy/HierarchyProviderSupport.hpp"

#include <lsp/types.h>

#include <memory>
#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

std::vector<const AstNode*> hierarchy_nodes_at(
    const References& references, const Document& document,
    lsp::Position position, const CancellationToken& cancelToken) {
  return references
      .findDeclaration(document,
                       document.textDocument().offsetAt(from_lsp(position)),
                       cancelToken)
      .targets;
}

const AstNode* hierarchy_node_of_item(const Documents& store,
                                      const References& references,
                                      const lsp::DocumentUri& uri,
                                      lsp::Position selection,
                                      const CancellationToken& cancelToken) {
  const std::shared_ptr<Document> document = store.getDocument(uri.toString());
  if (document == nullptr) return nullptr;
  const std::vector<const AstNode*> targets =
      hierarchy_nodes_at(references, *document, selection, cancelToken);
  const TextOffset offset =
      document->textDocument().offsetAt(from_lsp(selection));
  // A declaration's own name can resolve to its whole overload set.
  const AstNode* selected = nullptr;
  TextOffset selectedWidth = 0;
  for (const AstNode* target : targets) {
    throw_if_cancelled(cancelToken);
    if (target == nullptr || target->getDocument() != document.get()) continue;
    if (document->language != nullptr) {
      const auto name = document->language->nameProvider->getNameNode(
          *target, document.get());
      if (name && *name && name->begin() == offset) return target;
    }
    const CstNodeView node{&document->parseResult.cst(), target->cstNodeId};
    if (!node || offset < node.begin() || offset >= node.end()) continue;
    const TextOffset width = node.end() - node.begin();
    if (selected == nullptr || width < selectedWidth) {
      selected = target;
      selectedWidth = width;
    }
  }
  return selected;
}

}  // namespace cursiv
