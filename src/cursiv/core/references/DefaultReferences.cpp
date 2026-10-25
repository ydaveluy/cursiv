#include "cursiv/core/references/DefaultReferences.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"

namespace cursiv {

namespace {

const CoreServices* language_of(const Document& document,
                                const ServiceRegistry& registry) {
  return document.language != nullptr
             ? document.language
             : registry.findServices(document.uri,
                                     document.textDocument().languageId());
}

// What a reference under the caret denotes: the span of its name and every
// target it resolved to. A multi reference is visited once per target with one
// span, so the targets of the winning span accumulate.
//
// The narrowest enclosing span wins, not the first found: an answer that
// depended on the work list's order would depend on the grammar's field
// numbering.
[[nodiscard]] Declaration resolved_at(const Document& document,
                                      TextOffset offset) {
  Declaration found;
  document.parseResult.references->visitResolved(
      document.parseResult.cst(),
      [&](TextOffset begin, TextOffset end, const AstNode& target, bool) {
        if (offset < begin || offset >= end) return;
        if (found && found.span.end - found.span.begin <= end - begin) {
          // The same name, visited once more for another of its targets.
          if (begin == found.span.begin && end == found.span.end)
            found.targets.push_back(&target);
          return;
        }
        found.span = TextRange{begin, end};
        found.targets.clear();
        found.targets.push_back(&target);
      });
  return found;
}

// A name that denotes nothing still holds the caret: span set, targets empty.
// That is the site a user renames to fix a typo, so it must not lose the word
// to whatever stands beside it.
[[nodiscard]] std::optional<TextRange> unresolved_at(const Document& document,
                                                     TextOffset offset) {
  std::optional<TextRange> found;
  document.parseResult.references->visitUnresolved(
      document.parseResult.cst(),
      [&](RefState, TextOffset begin, TextOffset end, const ReferenceInfo&) {
        if (begin == end || offset < begin || offset >= end) return;
        if (found && found->end - found->begin <= end - begin) return;
        found = TextRange{begin, end};
      });
  return found;
}

// The innermost node at an offset: the leaf the offset sits in, then the
// node that owns it.
[[nodiscard]] const AstNode* node_at(const Document& document,
                                     TextOffset offset) {
  const Cst& cst = document.parseResult.cst();
  const NodeCount rank = cst.leaves_before(offset);
  if (rank >= cst.leaf_count()) return nullptr;
  const CstLeaf leaf = cst.leaf(rank);
  if (leaf.begin > offset) return nullptr;
  return owner_of_leaf(cst, leaf.id);
}

// Where a declaration is written: its name when it has one, its own record
// otherwise. A node declaring no name still has a site, and
// `includeDeclaration` asked for it.
[[nodiscard]] CstNodeView declaration_site(const AstNode& node,
                                           const Document& document,
                                           const NameProvider* names) {
  if (names != nullptr)
    if (const std::optional<CstNodeView> name =
            names->getNameNode(node, &document))
      return *name;
  const Cst& cst = document.parseResult.cst();
  if (node.cstNodeId == kNoNode || node.cstNodeId >= cst.size()) return {};
  return CstNodeView{&cst, node.cstNodeId};
}

}  // namespace

// A reference first, a declaration second: a caret on a reference is inside
// both regions, and the reference is the more specific answer.
Declaration DefaultReferences::at(const Document& document, TextOffset offset,
                                  const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  if (Declaration reference = resolved_at(document, offset); reference)
    return reference;
  if (const std::optional<TextRange> failed = unresolved_at(document, offset)) {
    Declaration found;
    found.span = *failed;
    return found;
  }

  // The caret is on a declaration only when it is on that declaration's name:
  // anywhere else inside it denotes nothing, as a space does.
  Declaration found;
  const AstNode* const node = node_at(document, offset);
  if (node == nullptr) return found;
  const CoreServices* const language =
      language_of(document, *services.shared.serviceRegistry);
  if (language == nullptr) return found;
  const std::optional<CstNodeView> name =
      language->nameProvider->getNameNode(*node, &document);
  if (!name || offset < name->begin() || offset >= name->end()) return found;

  found.span = TextRange{name->begin(), name->end()};
  // The group, not the member: a declaration a multi reference targets stands
  // for its whole overload set, whichever member the caret sat on.
  found.targets = getSelfNodes(*node, cancelToken);
  return found;
}

std::vector<const AstNode*> DefaultReferences::getSelfNodes(
    const AstNode& node, const CancellationToken& cancelToken) const {
  const Documents& store = *services.shared.documents;
  const IndexManager& index = *services.shared.indexManager;
  const Document* const home = node.getDocument();
  if (home == nullptr || node.cstNodeId == kNoNode) return {&node};
  // Incoming references define the group, including those from another
  // language whose reference kinds the declaring grammar does not describe.

  std::vector<const AstNode*> overloads;
  (void)index.visitReferences(
      home->id, node.cstNodeId, [&](const ReferenceDescription& incoming) {
        throw_if_cancelled(cancelToken);
        if (!incoming.multiReference) return true;
        const std::shared_ptr<Document> source =
            store.getDocument(incoming.sourceDocumentId);
        if (source == nullptr || source->parseResult.references == nullptr)
          return true;
        // The multi reference the description was made from, matched by the
        // span its name occupies in the source document.
        std::vector<const AstNode*> siblings;
        bool member = false;
        source->parseResult.references->visitResolved(
            source->parseResult.cst(), [&](TextOffset begin, TextOffset,
                                           const AstNode& target, bool multi) {
              if (!multi || begin != incoming.sourceBegin) return;
              siblings.push_back(&target);
              member = member || &target == &node;
            });
        if (!member) return true;
        overloads = std::move(siblings);
        return false;
      });
  if (!overloads.empty()) return overloads;
  return {&node};
}

std::vector<ReferenceDescription> DefaultReferences::getSelfReferences(
    const AstNode& node, const CancellationToken& cancelToken) const {
  std::vector<ReferenceDescription> rows;
  const ServiceRegistry& registry = *services.shared.serviceRegistry;
  for (const AstNode* const member : getSelfNodes(node, cancelToken)) {
    throw_if_cancelled(cancelToken);
    const Document* const home = member->getDocument();
    if (home == nullptr || member->cstNodeId == kNoNode) continue;
    // The name node comes from the member's own language: which members are
    // names is a fact about the grammar that built this node, not the one
    // that pointed at it.
    const CoreServices* const language = language_of(*home, registry);
    const NameProvider* const names =
        language == nullptr ? nullptr : language->nameProvider.get();
    if (const CstNodeView site = declaration_site(*member, *home, names))
      rows.push_back({.sourceDocumentId = home->id,
                      .sourceBegin = site.begin(),
                      .sourceEnd = site.end(),
                      .targetDocumentId = home->id,
                      .targetSymbolId = member->cstNodeId,
                      .local = true,
                      .multiReference = false});
  }
  return rows;
}

Declaration DefaultReferences::findDeclaration(
    const Document& document, TextOffset offset,
    const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  if (!document.hasAst() || document.parseResult.references == nullptr)
    return {};
  const DocumentState state = document.state.load();
  if (state >= DocumentState::ComputedScopes && state < DocumentState::Linked)
    document.parseResult.references->forceAt(document.parseResult.cst(), offset,
                                             cancelToken);
  Declaration found = at(document, offset, cancelToken);
  // One retry, and only when nothing contained the offset: a CST span is
  // half-open, so a click at the end of a name lands one past it. An unresolved
  // reference did contain it -- span set, targets empty -- and must not lose
  // the word to the neighbour one byte back.
  if (!found && found.span.end <= found.span.begin && offset > 0)
    found = at(document, offset - 1, cancelToken);
  return found;
}

std::vector<ReferenceDescription> DefaultReferences::findReferences(
    const AstNode& target, const FindReferencesOptions& options,
    const CancellationToken& cancelToken) const {
  std::vector<ReferenceDescription> references;
  throw_if_cancelled(cancelToken);
  const IndexManager& index = *services.shared.indexManager;

  // Which document declares it is the climb's answer: a node carries no
  // back-pointer, and a target reached through the global index routinely lives
  // in another file.
  const Document* const home = target.getDocument();
  if (home == nullptr || target.cstNodeId == kNoNode) return references;

  const auto wanted = [&](DocumentId source) {
    return options.documentId == kNoDocument || options.documentId == source;
  };

  // The whole group's declarations, not the member's alone: a target of a
  // multi reference stands for its overload set. Filtered like any row -- a
  // sibling declared in another document is not a highlight of this one.
  if (options.includeDeclaration)
    for (ReferenceDescription& row : getSelfReferences(target, cancelToken))
      if (wanted(row.sourceDocumentId)) references.push_back(row);

  (void)index.visitReferences(home->id, target.cstNodeId,
                              [&](const ReferenceDescription& described) {
                                throw_if_cancelled(cancelToken);
                                if (wanted(described.sourceDocumentId))
                                  references.push_back(described);
                                return true;
                              });

  // Reading order is owed rather than offered: the inverse table is built by
  // walking a hash map, so a rename's edits would otherwise arrive in an order
  // no test can pin.
  std::ranges::sort(references, [](const ReferenceDescription& a,
                                   const ReferenceDescription& b) {
    return a.sourceDocumentId != b.sourceDocumentId
               ? a.sourceDocumentId < b.sourceDocumentId
               : a.sourceBegin < b.sourceBegin;
  });
  return references;
}

}  // namespace cursiv
