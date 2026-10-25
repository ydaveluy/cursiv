#include "cursiv/lsp/navigation/DefaultDefinitionProvider.hpp"

#include <lsp/types.h>

#include <optional>
#include <utility>
#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "lsp/uri.h"

namespace cursiv {

// Delegates to `References::findDeclaration`, the same call rename,
// references and highlight make, so a caret on a declaration's own name
// resolves here exactly as it does for the other three.
std::optional<std::vector<lsp::LocationLink>>
DefaultDefinitionProvider::getDefinition(const Document& document,
                                         lsp::Position position,
                                         const CancellationToken& token) const {
  throw_if_cancelled(token);
  if (!document.hasAst()) return std::nullopt;

  const References& references = *_core.references;

  // The retry one byte back, the resolution on read and the "anything but
  // `Resolved` denotes nothing" rule all belong to the service, stated once
  // there.
  const Declaration found = references.findDeclaration(
      document, document.textDocument().offsetAt(from_lsp(position)), token);
  if (!found) return std::nullopt;
  std::vector<lsp::LocationLink> links;

  // The underline under the mouse: the name as written here, whether the caret
  // was on the reference or on the declaration's own name.
  const lsp::Range origin =
      to_lsp(document.textDocument().rangeAt(found.span.begin, found.span.end));

  // A multi-reference names several declarations at once and the protocol's
  // answer is a list either way, so the single-target family is the list of
  // one.
  for (const AstNode* target : found.targets) {
    throw_if_cancelled(token);
    if (target == nullptr) continue;
    // A node carries no back-pointer, so the owning document comes from
    // the store via `getDocument`; a target reached through the global
    // index routinely lives in another file, whose own positions still
    // convert.
    const Document* home = target->getDocument();
    if (home == nullptr || !home->hasAst()) continue;

    // Which members are names is a fact about the grammar that built the
    // target, not the one that pointed at it, so the name provider comes
    // from the target's own language.
    const CoreServices* language = home->language;
    const CstNodeView nameNode =
        language == nullptr ? CstNodeView{}
                            : language->nameProvider->getNameNode(*target, home)
                                  .value_or(CstNodeView{});

    lsp::LocationLink link;
    link.targetUri = lsp::Uri::parse(home->uri);
    // Target range is the whole declaration a peek shows; selection range
    // is where it puts the cursor. A node built by hand has no span of
    // its own, so its name node, if any, is the only thing left to show
    // or select.
    if (target->cstNodeId != kNoNode) {
      const CstNode& node = home->parseResult.cst()[target->cstNodeId];
      link.targetRange =
          to_lsp(home->textDocument().rangeAt(node.begin, node.end));
    } else if (nameNode) {
      link.targetRange = to_lsp(
          home->textDocument().rangeAt(nameNode.begin(), nameNode.end()));
    } else {
      continue;
    }
    link.targetSelectionRange = nameNode
                                    ? to_lsp(home->textDocument().rangeAt(
                                          nameNode.begin(), nameNode.end()))
                                    : link.targetRange;
    link.originSelectionRange = origin;
    links.push_back(std::move(link));
  }
  if (links.empty()) return std::nullopt;
  return links;
}

}  // namespace cursiv
