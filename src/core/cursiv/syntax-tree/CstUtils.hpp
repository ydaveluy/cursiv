#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"

// Where a member was written, and where a keyword was spelled, asked by
// name of an AST node, over the records of its document's tree. Pass the
// node's `document` to spare the walk up to the root.

namespace cursiv {

struct AstNode;
struct Document;

// The records of `node`'s own construction that write `feature`, in
// document order. A record that opens a node of its own ends the search down
// that branch. Empty for a tree that names no grammar.
[[nodiscard]] std::vector<CstNodeView> find_nodes_for_feature(
    const AstNode& node, std::string_view feature,
    const Document* document = nullptr);

// The `index`-th in source order, clamped to the last one written -- a list
// member is what the index is for. Nothing only when the member was never
// written at all.
[[nodiscard]] std::optional<CstNodeView> find_node_for_feature(
    const AstNode& node, std::string_view feature, std::size_t index = 0,
    const Document* document = nullptr);

// Every visible record of `node`'s own construction that spells `keyword`
// (in any case where the grammar allows), or `node`'s own record when it is
// that keyword. A record that opens a node of its own ends the search.
[[nodiscard]] std::vector<CstNodeView> find_nodes_for_keyword(
    const AstNode& node, std::string_view keyword,
    const Document* document = nullptr);

[[nodiscard]] std::optional<CstNodeView> find_node_for_keyword(
    const AstNode& node, std::string_view keyword, std::size_t index = 0,
    const Document* document = nullptr);

// Every hidden record of `node`'s own construction whose rule is
// `ruleName`, such as a comment. A nested node's records are its own.
[[nodiscard]] std::vector<CstNodeView> find_hidden_nodes(
    const AstNode& node, std::string_view ruleName,
    const Document* document = nullptr);

// The siblings between the branches containing the endpoints in their
// nearest common composite, in document order.
[[nodiscard]] std::vector<NodeId> interior_nodes(const Cst& cst, NodeId open,
                                                 NodeId close);

// The last visible leaf ending at or before `offset`. Trivia and repaired
// leaves are stepped over: the author wrote neither at a grammar position.
[[nodiscard]] CstLeaf find_leaf_before_offset(const Cst& cst,
                                              TextOffset offset);

// The innermost node owning `leaf`, or null when nothing owns it.
[[nodiscard]] AstNode* owner_of_leaf(const Cst& cst, NodeId leaf);
// The node that owns the caret, reached from the leaf before it. Null when no
// leaf answers or nothing owns it -- the caller's root stands in.
[[nodiscard]] AstNode* owner_at(const Cst& cst, TextOffset offset);

namespace detail {

// The word a caret writes in: the visible leaf ending at the caret whose last
// scalar is a word's, else the word leaf the caret stands at the start of or
// inside; empty between words. What completes there replaces it.
[[nodiscard]] CstLeaf word_at(const Cst& cst, TextOffset offset);

}  // namespace detail

}  // namespace cursiv
