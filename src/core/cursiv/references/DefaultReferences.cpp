#include "cursiv/references/DefaultReferences.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/IndexManager.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

namespace cursiv {

namespace {

const CoreServices* language_of(const Document& document,
                                const ServiceRegistry& registry) {
  return document.language != nullptr
             ? document.language
             : registry.findServices(document.uri,
                                     document.textDocument().languageId());
}

// What a reference under the caret denotes: its name's span and every target.
// The narrowest enclosing span wins, whatever the work list order. A name that
// resolved to nothing still keeps the caret: span set, targets empty.
[[nodiscard]] Declaration reference_at(const Document& document,
                                       TextOffset offset) {
  Declaration found;
  std::optional<TextRange> failed;
  document.parseResult.references->visitIn(
      document.parseResult.cst(), offset, offset,
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
      },
      [&](RefState, TextOffset begin, TextOffset end, const ReferenceInfo&) {
        if (begin == end || offset < begin || offset >= end) return;
        if (failed && failed->end - failed->begin <= end - begin) return;
        failed = TextRange{begin, end};
      });
  if (!found && failed) found.span = *failed;
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

// A row of one group: within it, the site and the symbol tell two apart.
struct RowHash {
  std::size_t operator()(const ReferenceDescription& row) const noexcept {
    const auto site = static_cast<std::size_t>(row.sourceBegin);
    const auto symbol = static_cast<std::size_t>(row.targetSymbolId);
    return site ^ (symbol + 0x9e3779b9u + (site << 6u) + (site >> 2u));
  }
};

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
  if (Declaration reference = reference_at(document, offset);
      reference || reference.span.end > reference.span.begin)
    return reference;

  // The caret is on a declaration only when it is on that declaration's name:
  // anywhere else inside it denotes nothing, as a space does.
  Declaration found;
  const AstNode* const node = node_at(document, offset);
  if (node == nullptr) return found;
  const CoreServices* const language =
      language_of(document, *shared.serviceRegistry);
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
  const Documents& store = *shared.documents;
  const IndexManager& index = *shared.indexManager;
  const Document* const declaringDocument = node.getDocument();
  if (declaringDocument == nullptr || node.cstNodeId == kNoNode) return {&node};
  // Incoming references define the group, including those from another
  // language whose reference kinds the declaring grammar does not describe.

  std::vector<const AstNode*> overloads;
  (void)index.visitReferences(
      declaringDocument->id, node.cstNodeId,
      [&](const ReferenceDescription& incoming) {
        throw_if_cancelled(cancelToken);
        if (!incoming.multiReference) return true;
        const std::shared_ptr<Document> source =
            store.getDocument(incoming.sourceDocumentId);
        if (source == nullptr || source->parseResult.references == nullptr)
          return true;
        // The multi reference the description was made from, matched by the
        // whole span its name occupies in the source document: another one
        // may start at the same offset.
        std::vector<const AstNode*> siblings;
        bool member = false;
        source->parseResult.references->visitIn(
            source->parseResult.cst(), incoming.sourceBegin,
            incoming.sourceBegin,
            [&](TextOffset begin, TextOffset end, const AstNode& target,
                bool multi) {
              if (!multi || begin != incoming.sourceBegin ||
                  end != incoming.sourceEnd)
                return;
              siblings.push_back(&target);
              member = member || &target == &node;
            },
            [](RefState, TextOffset, TextOffset, const ReferenceInfo&) {});
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
  // Hashed, not scanned: a group may hold thousands of members.
  std::unordered_set<ReferenceDescription, RowHash> drawn;
  const ServiceRegistry& registry = *shared.serviceRegistry;
  for (const AstNode* const member : getSelfNodes(node, cancelToken)) {
    throw_if_cancelled(cancelToken);
    const Document* const declaringDocument = member->getDocument();
    if (declaringDocument == nullptr || member->cstNodeId == kNoNode) continue;
    // The name node comes from the language that built the member, not from
    // the one that referenced it.
    const CoreServices* const language =
        language_of(*declaringDocument, registry);
    const NameProvider* const names =
        language == nullptr ? nullptr : language->nameProvider.get();
    const CstNodeView site =
        declaration_site(*member, *declaringDocument, names);
    if (!site) continue;
    const ReferenceDescription row{.sourceDocumentId = declaringDocument->id,
                                   .sourceBegin = site.begin(),
                                   .sourceEnd = site.end(),
                                   .targetDocumentId = declaringDocument->id,
                                   .targetSymbolId = member->cstNodeId,
                                   .local = true,
                                   .multiReference = false};
    // Two members built on one CST record are declared by one site.
    if (drawn.insert(row).second) rows.push_back(row);
  }
  return rows;
}

Declaration DefaultReferences::findDeclaration(
    const Document& document, TextOffset offset,
    const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  if (!document.hasAst() || document.parseResult.references == nullptr)
    return {};
  if (const DocumentState state = document.state.load();
      state >= DocumentState::ComputedScopes && state < DocumentState::Linked)
    document.parseResult.references->forceAt(document.parseResult.cst(), offset,
                                             cancelToken);
  Declaration found = at(document, offset, cancelToken);
  // One retry one byte back, only when nothing contained the offset: a click at
  // the end of a name falls one past it, as a CST span is half-open.
  if (!found && found.span.end <= found.span.begin && offset > 0)
    found = at(document, offset - 1, cancelToken);
  return found;
}

std::vector<ReferenceDescription> DefaultReferences::findReferences(
    const AstNode& target, const FindReferencesOptions& options,
    const CancellationToken& cancelToken) const {
  std::vector<ReferenceDescription> references;
  throw_if_cancelled(cancelToken);
  const IndexManager& index = *shared.indexManager;

  // The declaring document, found through the containers: a target from the
  // global index often lives in another file.
  const Document* const declaringDocument = target.getDocument();
  if (declaringDocument == nullptr || target.cstNodeId == kNoNode)
    return references;

  const auto wanted = [&](DocumentId source) {
    return options.documentId == kNoDocument || options.documentId == source;
  };

  // The whole group's declarations, not the member's alone: a target of a
  // multi reference stands for its overload set. Filtered like any row -- a
  // sibling declared in another document is not a highlight of this one.
  if (options.includeDeclaration)
    for (const ReferenceDescription& row :
         getSelfReferences(target, cancelToken))
      if (wanted(row.sourceDocumentId)) references.push_back(row);

  (void)index.visitReferences(declaringDocument->id, target.cstNodeId,
                              [&](const ReferenceDescription& described) {
                                throw_if_cancelled(cancelToken);
                                if (wanted(described.sourceDocumentId))
                                  references.push_back(described);
                                return true;
                              });

  // Sorted: the inverse table comes from a hash map, and a rename's edits need
  // a stable order.
  std::ranges::sort(references, [](const ReferenceDescription& a,
                                   const ReferenceDescription& b) {
    return a.sourceDocumentId != b.sourceDocumentId
               ? a.sourceDocumentId < b.sourceDocumentId
               : a.sourceBegin < b.sourceBegin;
  });
  return references;
}

}  // namespace cursiv
