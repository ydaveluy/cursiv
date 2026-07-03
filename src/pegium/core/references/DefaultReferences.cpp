#include <pegium/core/references/DefaultReferences.hpp>

#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>

namespace pegium::references {

namespace {

// Tracks the smallest CST span fully enclosing `target`. Returns true — and
// tightens `bestSpan` — when `candidate` encloses `target` (inclusive
// boundaries) with a strictly smaller span than any seen so far, signalling the
// caller to record `candidate`'s owner as the new best. Earlier candidates win
// on ties.
[[nodiscard]] bool tightens_enclosing_span(const CstNodeView &candidate,
                                           const CstNodeView &target,
                                           TextOffset &bestSpan) {
  if (target.getBegin() < candidate.getBegin() ||
      candidate.getEnd() < target.getEnd()) {
    return false;
  }
  const auto span = candidate.getEnd() - candidate.getBegin();
  if (span >= bestSpan) {
    return false;
  }
  bestSpan = span;
  return true;
}

const AbstractReference *
find_reference_at_cst_node(const workspace::Document &document,
                           const CstNodeView &selectedNode) {
  const AbstractReference *best = nullptr;
  TextOffset bestSpan = std::numeric_limits<TextOffset>::max();
  for (const auto &handle : document.parseResult.references) {
    const auto &reference = *handle.getConst();
    if (const auto refNode = reference.getRefNode();
        refNode.valid() &&
        tightens_enclosing_span(refNode, selectedNode, bestSpan)) {
      best = &reference;
    }
  }
  return best;
}

void append_reference_targets(const AbstractReference &reference,
                              const pegium::CoreServices &services,
                              std::vector<const AstNode *> &results) {
  const auto &documents = *services.shared.workspace.documents;
  assert(reference.getContainer() != nullptr);
  const auto &currentDocument = getDocument(*reference.getContainer());
  std::unordered_set<workspace::NodeKey, workspace::NodeKeyHash> seen;
  const auto append =
      [&results, &seen, &documents,
       &currentDocument](const workspace::AstNodeDescription &description) {
        if (!seen.insert(workspace::NodeKey::of(description)).second) {
          return;
        }
        results.push_back(std::addressof(
            workspace::resolve_ast_node(documents, description,
                                        currentDocument)));
      };

  if (reference.isMultiReference()) {
    const auto &multi = static_cast<const AbstractMultiReference &>(reference);
    for (std::size_t index = 0; index < multi.resolvedDescriptionCount(); ++index) {
      append(multi.resolvedDescriptionAt(index));
    }
    return;
  }

  const auto &single = static_cast<const AbstractSingleReference &>(reference);
  if (single.isResolved()) {
    append(single.resolvedDescription());
  }
}

const AstNode *find_named_declaration(const AstNode &root,
                                      const CstNodeView &selectedNode,
                                      const pegium::CoreServices &services) {
  const auto *nameProvider = services.references.nameProvider.get();

  const AstNode *selected = nullptr;
  TextOffset selectedSpan = std::numeric_limits<TextOffset>::max();

  const auto consider = [&](const AstNode &node) {
    if (const auto candidateNode = declaration_site_node(node, *nameProvider);
        candidateNode.has_value() &&
        tightens_enclosing_span(*candidateNode, selectedNode, selectedSpan)) {
      selected = &node;
    }
  };

  consider(root);
  for (const auto *node : root.getAllContent()) {
    consider(*node);
  }

  return selected;
}

const AbstractReference *
find_reference_at_offset(const workspace::Document &document,
                         TextOffset offset) {
  for (const auto &handle : document.parseResult.references) {
    const auto &reference = *handle.getConst();
    if (const auto refNode = reference.getRefNode();
        refNode.valid() && refNode.getBegin() == offset) {
      return &reference;
    }
  }
  return nullptr;
}

// Expands a declaration node into every logical sibling declared under the same
// name through a multi-reference. When `node` is the target of a multi-reference,
// that reference also targets its siblings, so it yields the whole group; for a
// plain reference (or none) it returns just `node`.
std::vector<const AstNode *>
get_self_nodes(const AstNode &node, const pegium::CoreServices &services,
               const std::vector<workspace::ReferenceDescription> &incoming) {
  const auto &documents = *services.shared.workspace.documents;

  // `node` may be targeted by several references; only a multi-reference pulls
  // in the sibling group. Scan every incoming reference — not just the first —
  // so a plain reference enumerated ahead of the multi-reference does not mask
  // the siblings.
  for (const auto &reference : incoming) {
    // Only a multi-reference pulls in siblings. The indexed flag lets us skip
    // plain references in O(1); resolving the actual reference (a linear scan of
    // the source document via find_reference_at_offset) is done only for the
    // few multi-references, so this stays O(incoming) instead of O(incoming x
    // references) on a high-fan-in symbol.
    if (!reference.multiReference) {
      continue;
    }
    const auto sourceDocument =
        documents.getDocument(reference.sourceDocumentId);
    if (sourceDocument == nullptr) {
      continue;
    }
    const auto *sourceReference =
        find_reference_at_offset(*sourceDocument, reference.sourceOffset);
    if (sourceReference == nullptr || !sourceReference->isMultiReference()) {
      continue;
    }
    std::vector<const AstNode *> siblings;
    append_reference_targets(*sourceReference, services, siblings);
    if (std::ranges::find(siblings, &node) != siblings.end()) {
      return siblings;
    }
  }
  return {&node};
}

std::vector<const AstNode *>
get_self_nodes(const AstNode &node, const pegium::CoreServices &services) {
  const auto *indexManager = services.shared.workspace.indexManager.get();
  const auto &document = getDocument(node);
  const auto targetKey = workspace::NodeKey{
      .documentId = document.id, .symbolId = document.makeSymbolId(node)};
  return get_self_nodes(node, services,
                        indexManager->findAllReferences(targetKey));
}

} // namespace

std::vector<const AstNode *>
DefaultReferences::findDeclarations(const CstNodeView &sourceCstNode) const {
  std::vector<const AstNode *> results;
  assert(sourceCstNode.valid());
  const auto &document = sourceCstNode.root().getDocument();

  if (const auto *reference =
          find_reference_at_cst_node(document, sourceCstNode);
      reference != nullptr) {
    append_reference_targets(*reference, services, results);
    return results;
  }

  if (document.hasAst()) {
    if (const auto *declaration = find_named_declaration(
            *document.parseResult.value, sourceCstNode, services);
        declaration != nullptr) {
      return get_self_nodes(*declaration, services);
    }
  }
  return results;
}

std::vector<CstNodeView>
DefaultReferences::findDeclarationNodes(const CstNodeView &sourceCstNode) const {
  std::vector<CstNodeView> results;
  const auto *nameProvider = services.references.nameProvider.get();
  for (const auto *node : findDeclarations(sourceCstNode)) {
    results.push_back(required_declaration_site_node(*node, *nameProvider));
  }
  return results;
}

std::vector<workspace::ReferenceDescription>
DefaultReferences::findReferences(const AstNode &targetNode,
                                  const FindReferencesOptions &options) const {
  const auto *indexManager = services.shared.workspace.indexManager.get();
  const auto *nameProvider = services.references.nameProvider.get();
  const auto &document = getDocument(targetNode);

  // The target's incoming-reference set drives both the self-node expansion and
  // the final emit loop, so query the index once and reuse the result.
  const auto targetKey = workspace::NodeKey{
      .documentId = document.id,
      .symbolId = document.makeSymbolId(targetNode),
  };
  const auto incoming = indexManager->findAllReferences(targetKey);

  std::vector<workspace::ReferenceDescription> results;
  if (options.includeDeclaration) {
    // Expand the target into all logical siblings (multi-reference) so each
    // sibling declaration is reported as a self-reference.
    for (const auto *selfNode : get_self_nodes(targetNode, services, incoming)) {
      const auto &selfDocument = getDocument(*selfNode);
      // Honor the document filter for self-references too: the indexed-reference
      // loop below filters by options.documentId, so a self-node from another
      // document would otherwise produce a highlight range against the wrong doc.
      if (options.documentId.has_value() &&
          selfDocument.id != *options.documentId) {
        continue;
      }
      const auto declarationNode =
          required_declaration_site_node(*selfNode, *nameProvider);
      results.push_back(workspace::ReferenceDescription{
          .sourceDocumentId = selfDocument.id,
          .sourceOffset = declarationNode.getBegin(),
          .sourceLength = declarationNode.getEnd() - declarationNode.getBegin(),
          .local = true,
          .targetDocumentId = selfDocument.id,
          .targetSymbolId = selfDocument.makeSymbolId(*selfNode),
      });
    }
  }

  for (const auto &reference : incoming) {
    if (options.documentId.has_value() &&
        reference.sourceDocumentId != *options.documentId) {
      continue;
    }
    results.push_back(reference);
  }

  return results;
}

} // namespace pegium::references
