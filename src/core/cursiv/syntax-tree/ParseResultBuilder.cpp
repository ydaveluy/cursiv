#include "cursiv/syntax-tree/ParseResultBuilder.hpp"

#include <algorithm>
#include <memory>

#include "cursiv/syntax-tree/DefaultDocumentReferences.hpp"

namespace cursiv {

ParseResultBuilder::ParseResultBuilder(ParseResult& out, const detail::AstMetaData& meta)
    : _out(out), _tree(*out._tree), _arena(_tree.nodes.get_allocator().resource()) {
  if (meta.referenceBound != 0) {
    _out.references = std::make_unique<DefaultDocumentReferences>(meta);
    _resolver = _out.references.get();
    _handles = &_out.references->handles;
  }
  _out.accepted = cst().accepted();
  const auto repairs = cst().diagnostics();
  _out.diagnostics.assign(repairs.begin(), repairs.end());
}

void ParseResultBuilder::reserve(std::size_t nodes, std::size_t references) {
  _tree.nodes.reserve(nodes);
  if (_handles != nullptr) _handles->reserve(references);
}

// A node is kept where its parents lead to the root, whatever order the
// nodes were made in. Each path walked is cut short for the next: a node on
// it points to the root, or to nothing where it is not kept, and the links
// kept are put back.
void ParseResultBuilder::prune(AstNode* root) {
  const std::span<AstNode*> nodes = _tree.nodes;
  std::vector<std::pair<AstNode*, AstNode*>> parents;
  parents.reserve(nodes.size());
  for (AstNode* node : nodes) {
    const AstNode* at = node;
    while (at != root && at->parent != nullptr && at->parent != root) at = at->parent;
    const bool kept = at == root || at->parent == root;
    while (node != root && node->parent != nullptr && node->parent != root) {
      AstNode* const parent = node->parent;
      if (kept) {
        parents.emplace_back(node, parent);
        node->parent = root;
      } else {
        node->parent = nullptr;
      }
      node = parent;
    }
  }
  for (const auto& [node, parent] : parents) node->parent = parent;
  const auto discarded = [root](const AstNode* node) {
    return node != nullptr && node != root && node->parent == nullptr;
  };
  std::erase_if(_tree.origins, [&](const auto& origin) { return discarded(origin.owner); });
  if (_handles != nullptr)
    for (detail::RefHandle& handle : *_handles)
      if (discarded(handle.owner)) handle.owner = nullptr;
  AstNode** kept = nodes.data();
  for (AstNode* const node : nodes)
    if (discarded(node))
      node->~AstNode();
    else
      *kept++ = node;
  _tree.nodes.resize(static_cast<std::size_t>(kept - nodes.data()));
}

void ParseResultBuilder::finish(AstNode* root, AstBuildError error) {
  _out.value = root;
  _out.error = error;
  if (root != nullptr) {
    root->parent = nullptr;
    // Every node is on the list and at most one member took it: one more
    // than the members took is the root alone.
    if (_tree.nodes.size() != _attached + 1) prune(root);
  }
  if (!std::ranges::is_sorted(_tree.origins, {}, &ParseResult::PropertyOrigin::record))
    std::ranges::sort(_tree.origins, {}, &ParseResult::PropertyOrigin::record);
  if (_unsorted) {
    const auto record = [](const AstNode* node) { return node->cstNodeId; };
    if (!std::ranges::is_sorted(_tree.nodes, {}, record))
      std::ranges::stable_sort(_tree.nodes, {}, record);
  }
}

}  // namespace cursiv
