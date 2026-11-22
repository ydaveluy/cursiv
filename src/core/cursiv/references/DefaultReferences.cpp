#include "cursiv/references/DefaultReferences.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/IndexManager.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

namespace cursiv {

namespace {

// What a reference under the caret denotes: its name's span and every target.
// The narrowest enclosing span wins, whatever the work list order. A name that
// resolved to nothing still keeps the caret: span set, targets empty.
[[nodiscard]] Declaration reference_at(const Document& document,
                                       TextOffset offset) {
  Declaration found;
  const DocumentReferences* const references =
      document.parseResult.references.get();
  if (references == nullptr) return found;
  std::optional<TextRange> failed;
  references->visitIn(
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
  if (failed && (!found || failed->end - failed->begin <
                              found.span.end - found.span.begin)) {
    found.span = *failed;
    found.targets.clear();
  }
  return found;
}

// The innermost node at an offset: the leaf the offset sits in, then the
// node that owns it.
[[nodiscard]] const AstNode* node_at(const Document& document,
                                     TextOffset offset) {
  const Cst& cst = document.parseResult.cst();
  const NodeId after = cst.leafAfter(offset);
  if (after == cst.size()) return nullptr;
  const CstLeaf leaf = cst.asLeaf(after);
  if (leaf.begin > offset) return nullptr;
  return ownerOfLeaf(cst, leaf.id);
}

// The targets of the reference a row describes, matched by the whole span its
// name occupies: another one may start at the same offset.
template <class Visit>
void visit_row_targets(const Documents& store, const ReferenceDescription& row,
                       Visit&& visit) {
  const std::shared_ptr<Document> source =
      store.getDocument(row.sourceDocumentId);
  if (source == nullptr || source->parseResult.references == nullptr) return;
  source->parseResult.references->visitIn(
      source->parseResult.cst(), row.sourceBegin, row.sourceBegin,
      [&](TextOffset begin, TextOffset end, const AstNode& target, bool multi) {
        if (begin == row.sourceBegin && end == row.sourceEnd)
          visit(target, multi);
      },
      [](RefState, TextOffset, TextOffset, const ReferenceInfo&) {});
}

// Whether another node is built on `node`'s CST record: the index then keys
// the rows of both alike.
[[nodiscard]] bool shares_record(const AstNode& node,
                                 const Document& document) {
  const OwnedNodes nodes = document.parseResult.nodes();
  return std::ranges::equal_range(
             nodes, node.cstNodeId, {},
             [](const AstNode* owned) { return owned->cstNodeId; })
             .size() > 1;
}

// Whether a row's reference names `node` itself, and not only its record.
[[nodiscard]] bool names_node(const Documents& store,
                              const ReferenceDescription& row,
                              const AstNode& node, bool sharedRecord) {
  bool names = !sharedRecord;
  if (sharedRecord)
    visit_row_targets(store, row, [&](const AstNode& to, bool) {
      names = names || &to == &node;
    });
  return names;
}

// The multi reference that makes `node`'s group: the first, in (document,
// offset) order, that has `node` among its targets. Incoming references count
// from any language, even one the declaring grammar does not describe.
[[nodiscard]] std::optional<ReferenceDescription> group_reference(
    const Documents& store, const IndexManager& index, const AstNode& node,
    const CancellationToken& cancelToken) {
  const Document* const declaringDocument = node.getDocument();
  if (declaringDocument == nullptr || node.cstNodeId == kNoNode) return {};
  const bool sharedRecord = shares_record(node, *declaringDocument);
  std::optional<ReferenceDescription> group;
  (void)index.visitReferences(
      declaringDocument->id, node.cstNodeId,
      [&](const ReferenceDescription& incoming) {
        throwIfCancelled(cancelToken);
        if (!incoming.multiReference ||
            !names_node(store, incoming, node, sharedRecord))
          return true;
        group = incoming;
        return false;
      });
  return group;
}

// A row of one group: its document, site and symbol tell two apart.
struct RowHash {
  std::size_t operator()(const ReferenceDescription& row) const noexcept {
    auto h = static_cast<std::size_t>(row.sourceDocumentId);
    for (const std::size_t part :
         {std::size_t{row.sourceBegin}, std::size_t{row.targetSymbolId}})
      h ^= part + 0x9e3779b9u + (h << 6u) + (h >> 2u);
    return h;
  }
};

// Where a declaration is written: its name when it has one, its own record
// otherwise. A node declaring no name still has a site, and
// `includeDeclaration` asked for it.
[[nodiscard]] CstNodeView declaration_site(const AstNode& node,
                                           const Document& document,
                                           const NameProvider* names) {
  if (names != nullptr)
    if (const CstNodeView name = names->getNameNode(node, &document))
      return name;
  const Cst& cst = document.parseResult.cst();
  if (node.cstNodeId == kNoNode || node.cstNodeId >= cst.size()) return {};
  return CstNodeView{&cst, node.cstNodeId};
}

}  // namespace

// A reference first, a declaration second: a caret on a reference is inside
// both regions, and the reference is the more specific answer.
Declaration DefaultReferences::at(const Document& document, TextOffset offset,
                                  const CancellationToken& cancelToken) const {
  throwIfCancelled(cancelToken);
  if (Declaration reference = reference_at(document, offset);
      reference || reference.span.end > reference.span.begin)
    return reference;

  // The caret is on a declaration only when it is on that declaration's name:
  // anywhere else inside it denotes nothing, as a space does.
  Declaration found;
  const AstNode* const node = node_at(document, offset);
  if (node == nullptr) return found;
  const CoreServices* const language = document.language;
  if (language == nullptr) return found;
  const CstNodeView name =
      language->nameProvider->getNameNode(*node, &document);
  if (!name || offset < name.begin() || offset >= name.end()) return found;

  found.span = TextRange{name.begin(), name.end()};
  // The group, not the member: a declaration a multi reference targets stands
  // for its whole overload set, whichever member the caret sat on.
  found.targets = getSelfNodes(*node, cancelToken);
  return found;
}

std::vector<const AstNode*> DefaultReferences::getSelfNodes(
    const AstNode& node, const CancellationToken& cancelToken) const {
  std::vector<const AstNode*> members;
  if (const std::optional<ReferenceDescription> group = group_reference(
          *shared.documents, *shared.indexManager, node, cancelToken))
    visit_row_targets(*shared.documents, *group,
                      [&](const AstNode& target, bool multi) {
                        if (multi) members.push_back(&target);
                      });
  if (members.empty()) members.push_back(&node);
  return members;
}

std::vector<ReferenceDescription> DefaultReferences::getSelfReferences(
    const AstNode& node, const CancellationToken& cancelToken) const {
  std::vector<ReferenceDescription> rows;
  // Hashed, not scanned: a group may hold thousands of members.
  std::unordered_set<ReferenceDescription, RowHash> drawn;
  for (const AstNode* const member : getSelfNodes(node, cancelToken)) {
    throwIfCancelled(cancelToken);
    const Document* const declaringDocument = member->getDocument();
    if (declaringDocument == nullptr || member->cstNodeId == kNoNode) continue;
    // The name node comes from the language that built the member, not from
    // the one that referenced it.
    const CoreServices* const language = declaringDocument->language;
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

Declaration DefaultReferences::findDeclarations(
    const Document& document, TextOffset offset,
    const CancellationToken& cancelToken) const {
  throwIfCancelled(cancelToken);
  if (!document.hasAst()) return {};
  if (const DocumentState state = document.state.load();
      document.parseResult.references != nullptr &&
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
    std::span<const AstNode* const> targets,
    const FindReferencesOptions& options,
    const CancellationToken& cancelToken) const {
  std::vector<ReferenceDescription> references;
  const Documents& store = *shared.documents;
  const IndexManager& index = *shared.indexManager;
  const auto wanted = [&](DocumentId source) {
    return options.documentId == kNoDocument || options.documentId == source;
  };
  const auto site = [](const ReferenceDescription& row) {
    return std::tuple{row.sourceDocumentId, row.sourceBegin, row.sourceEnd};
  };

  // The groups drawn, by the site of the multi reference that makes each, with
  // their members: a member asked later finds its group drawn.
  std::map<std::tuple<DocumentId, TextOffset, TextOffset>,
           std::unordered_set<const AstNode*>>
      drawn;
  const auto undrawn = [&](const AstNode& target) {
    const std::optional<ReferenceDescription> group =
        group_reference(store, index, target, cancelToken);
    if (!group) return true;
    const auto [at, added] = drawn.try_emplace(site(*group));
    if (added)
      for (const AstNode* const member : getSelfNodes(target, cancelToken))
        at->second.insert(member);
    return added || !at->second.contains(&target);
  };

  for (const AstNode* const target : targets) {
    throwIfCancelled(cancelToken);
    // The declaring document, found through the containers: a target from the
    // global index often lives in another file.
    const Document* const declaringDocument = target->getDocument();
    if (declaringDocument == nullptr || target->cstNodeId == kNoNode) continue;

    // The whole group's declarations, once per group, filtered like any row: a
    // target of a multi reference stands for its overload set.
    if (options.includeDeclaration && undrawn(*target))
      for (const ReferenceDescription& row :
           getSelfReferences(*target, cancelToken))
        if (wanted(row.sourceDocumentId)) references.push_back(row);

    const bool sharedRecord = shares_record(*target, *declaringDocument);
    (void)index.visitReferences(
        declaringDocument->id, target->cstNodeId,
        [&](const ReferenceDescription& described) {
          throwIfCancelled(cancelToken);
          if (names_node(store, described, *target, sharedRecord) &&
              wanted(described.sourceDocumentId))
            references.push_back(described);
          return true;
        });
  }

  // Sorted, as the inverse table comes from a hash map and a rename's edits
  // need a stable order; stably, so a site keeps the first target's row.
  std::ranges::stable_sort(references, {}, site);
  const auto repeats = std::ranges::unique(references, {}, site);
  references.erase(repeats.begin(), repeats.end());
  return references;
}

}  // namespace cursiv
