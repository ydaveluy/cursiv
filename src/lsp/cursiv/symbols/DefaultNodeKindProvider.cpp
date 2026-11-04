#include "cursiv/symbols/DefaultNodeKindProvider.hpp"

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "lsp/types.h"

namespace cursiv {

lsp::SymbolKind DefaultNodeKindProvider::getSymbolKind(const AstNode&) const {
  return lsp::SymbolKind::Field;
}

lsp::SymbolKind DefaultNodeKindProvider::getSymbolKind(
    const AstNodeDescription&) const {
  return lsp::SymbolKind::Field;
}

lsp::CompletionItemKind DefaultNodeKindProvider::getCompletionItemKind(
    const AstNode&) const {
  return lsp::CompletionItemKind::Reference;
}

lsp::CompletionItemKind DefaultNodeKindProvider::getCompletionItemKind(
    const AstNodeDescription&) const {
  return lsp::CompletionItemKind::Reference;
}

}  // namespace cursiv
