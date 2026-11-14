#include "cursiv/references/DefaultScopeComputation.hpp"


#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/AstNodeDescriptionProvider.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/LocalSymbols.hpp"

namespace cursiv {

std::vector<AstNodeDescription> DefaultScopeComputation::collectExportedSymbols(
    const Document& document, const CancellationToken& cancelToken) const {
  AstNode* const root = document.parseResult.value;
  if (root == nullptr) return {};

  // The root, then what it directly contains, asked of the grammar: a document
  // exports its top level only.
  return collectExportedSymbolsForNode(
      *root, document,
      [&](function_ref<void(AstNode&)> visit) {
        services.astReflection->visitProperties(
            *root, [&](std::string_view, std::uint32_t, AstNode& child) {
              visit(child);
            });
      },
      cancelToken);
}

std::vector<AstNodeDescription>
DefaultScopeComputation::collectExportedSymbolsForNode(
    AstNode& parent, const Document& document,
    function_ref<void(function_ref<void(AstNode&)>)> children,
    const CancellationToken& cancelToken) const {
  std::vector<AstNodeDescription> exported;
  addExportedSymbol(parent, exported, document);
  children([&](AstNode& node) {
    throw_if_cancelled(cancelToken);
    addExportedSymbol(node, exported, document);
  });
  return exported;
}

void DefaultScopeComputation::addExportedSymbol(
    AstNode& node, std::vector<AstNodeDescription>& exported,
    const Document& document) const {
  if (auto description = services.astNodeDescriptionProvider->createDescription(
          node, {}, &document))
    exported.push_back(*std::move(description));
}

LocalSymbols DefaultScopeComputation::collectLocalSymbols(
    const Document& document, const CancellationToken& cancelToken) const {
  LocalSymbols symbols;
  for (AstNode* const node : document.parseResult.nodes()) {
    throw_if_cancelled(cancelToken);
    addLocalSymbol(*node, document, symbols);
  }
  // Collected flat in declaration order, grouped and indexed once: nothing is
  // inserted after this, so nothing moves and the addresses handed out below
  // are final.
  symbols.buildIndex();
  return symbols;
}

void DefaultScopeComputation::addLocalSymbol(AstNode& node,
                                             const Document& document,
                                             LocalSymbols& symbols) const {
  // The root's container is the document itself, which is nobody's scope.
  const AstNode* const container = node.parent;
  if (container == nullptr || container == &document) return;
  if (auto description = services.astNodeDescriptionProvider->createDescription(
          node, {}, &document))
    symbols.add(container, *std::move(description));
}

}  // namespace cursiv
