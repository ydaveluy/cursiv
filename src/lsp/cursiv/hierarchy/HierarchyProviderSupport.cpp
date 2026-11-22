#include "cursiv/hierarchy/HierarchyProviderSupport.hpp"

#include <lsp/types.h>

#include <memory>
#include <vector>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv {

std::vector<const AstNode*> hierarchyNodesAt(
    const References& references, const Document& document,
    lsp::Position position, const CancellationToken& cancelToken) {
  return references
      .findDeclarations(document,
                        document.textDocument().offsetAt(fromLsp(position)),
                        cancelToken)
      .targets;
}

const AstNode* hierarchyNodeOfItem(const Documents& store,
                                      const References& references,
                                      const lsp::DocumentUri& uri,
                                      lsp::Position selection,
                                      const CancellationToken& cancelToken) {
  const std::shared_ptr<Document> document = store.getDocument(uri.toString());
  if (document == nullptr) return nullptr;
  const std::vector<const AstNode*> targets =
      hierarchyNodesAt(references, *document, selection, cancelToken);
  const TextOffset offset =
      document->textDocument().offsetAt(fromLsp(selection));
  // A declaration's own name can resolve to its whole overload set.
  const AstNode* selected = nullptr;
  TextOffset selectedWidth = 0;
  for (const AstNode* target : targets) {
    throwIfCancelled(cancelToken);
    if (target == nullptr || target->getDocument() != document.get()) continue;
    if (document->language != nullptr) {
      const auto name = document->language->nameProvider->getNameNode(
          *target, document.get());
      if (name && name.begin() == offset) return target;
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
