#include "cursiv/navigation/DefaultDefinitionProvider.hpp"

#include <lsp/types.h>

#include <optional>
#include <utility>
#include <vector>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "lsp/uri.h"

namespace cursiv {

// Uses `References::findDeclarations`, as rename, references and highlight do,
// so a caret resolves the same way for all four.
std::optional<std::vector<lsp::LocationLink>>
DefaultDefinitionProvider::getDefinition(const Document& document,
                                         lsp::Position position,
                                         const CancellationToken& token) const {
  throw_if_cancelled(token);
  if (!document.hasAst()) return std::nullopt;

  const References& references = *services.references;

  const Declaration found = references.findDeclarations(
      document, document.textDocument().offsetAt(from_lsp(position)), token);
  if (!found) return std::nullopt;
  std::vector<lsp::LocationLink> links;

  // The underline under the mouse: the name as written here, whether the caret
  // was on the reference or on the declaration's own name.
  const lsp::Range origin =
      to_lsp(document.textDocument().rangeAt(found.span.begin, found.span.end));

  // A multi-reference gives one link per declaration.
  for (const AstNode* target : found.targets) {
    throw_if_cancelled(token);
    if (target == nullptr) continue;
    // The target's own document, often another file.
    const Document* targetDocument = target->getDocument();
    if (targetDocument == nullptr || !targetDocument->hasAst()) continue;

    // Names are read by the target's language, not the asking one.
    const CoreServices* language = targetDocument->language;
    const CstNodeView nameNode =
        language == nullptr
            ? CstNodeView{}
            : language->nameProvider->getNameNode(*target, targetDocument);

    lsp::LocationLink link;
    link.targetUri = lsp::Uri::parse(targetDocument->uri);
    // The target range is the whole declaration, the selection range its name.
    // A node built by hand has no span: its name node is all there is to show.
    if (target->cstNodeId != kNoNode) {
      const CstNode& node =
          targetDocument->parseResult.cst()[target->cstNodeId];
      link.targetRange =
          to_lsp(targetDocument->textDocument().rangeAt(node.begin, node.end));
    } else if (nameNode) {
      link.targetRange = to_lsp(targetDocument->textDocument().rangeAt(
          nameNode.begin(), nameNode.end()));
    } else {
      continue;
    }
    link.targetSelectionRange =
        nameNode ? to_lsp(targetDocument->textDocument().rangeAt(
                       nameNode.begin(), nameNode.end()))
                 : link.targetRange;
    link.originSelectionRange = origin;
    links.push_back(std::move(link));
  }
  if (links.empty()) return std::nullopt;
  return links;
}

}  // namespace cursiv
