#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"

// Where a member was written, and where a keyword was spelled, asked by name.
// The tree names the grammar that stamped it and the records that opened a
// node, so a question is asked of a node and nothing else -- which is what
// lets a formatter and a diagnostic anchor be written once for the tree.

namespace cursiv {

struct AstNode;

// The direct children of `node` that the grammar says write `feature`, in
// document order. A record that opens a node of its own ends the search down
// that branch: a nested declaration answers for itself, never for its
// parent. Empty for a tree that names no grammar.
[[nodiscard]] std::vector<CstNodeView> find_nodes_for_feature(
    const CstNodeView& node, std::string_view feature);

// The `index`-th in source order, clamped to the last one written -- a list
// member is what the index is for. Nothing only when the member was never
// written at all.
[[nodiscard]] std::optional<CstNodeView> find_node_for_feature(
    const CstNodeView& node, std::string_view feature, std::size_t index = 0);

// Every visible record under `node` whose grammar element spells `keyword`, or
// `node` alone when its record is that keyword. Hidden records are skipped: a
// keyword inside a comment was not spelled, and a record that opens a node of
// its own ends the search down that branch -- a rule about "my closing brace"
// must never select a child's, even a child that is nothing but its keyword.
[[nodiscard]] std::vector<CstNodeView> find_nodes_for_keyword(
    const CstNodeView& node, std::string_view keyword);

[[nodiscard]] std::optional<CstNodeView> find_node_for_keyword(
    const CstNodeView& node, std::string_view keyword, std::size_t index = 0);

// Every hidden record under `node` whose rule is `ruleName` -- a comment class
// is what this is for. Same rule down a branch: a record that opens a node of
// its own answers for itself, so a container never selects a nested one's.
[[nodiscard]] std::vector<CstNodeView> find_hidden_nodes(
    const CstNodeView& node, std::string_view ruleName);

// The siblings between the branches containing the endpoints in their
// nearest common composite, in document order.
[[nodiscard]] std::vector<NodeId> interior_nodes(const Cst& cst, NodeId open,
                                                 NodeId close);

// The last visible leaf ending at or before `offset`, found by bisecting the
// leaves the tree states outright. Trivia and repaired leaves are stepped
// over: neither is something the author wrote at a grammar position, and
// answering from one names no continuation. The record says which is which
// -- hidden, recovered, zero width -- so no grammar is read.
[[nodiscard]] CstLeaf find_leaf_before_offset(const Cst& cst,
                                              TextOffset offset);

// The word a caret writes in: the visible leaf ending at the caret whose last
// scalar is a word's, else the word leaf the caret stands at the start of or
// inside; empty between words. What completes there replaces it.
[[nodiscard]] CstLeaf word_at(const Cst& cst, TextOffset offset);

// The innermost node owning `leaf`. Postorder puts a record's ancestors
// after it, so the first entry of the tree's map at or past the leaf is
// either that owner or a descendant of it: from there the AST parents climb
// to it. Null when nothing owns the leaf.
[[nodiscard]] AstNode* owner_of_leaf(const Cst& cst, NodeId leaf);
// The node that owns the caret, reached from the leaf before it. Null when no
// leaf answers or nothing owns it -- the caller's root stands in.
[[nodiscard]] AstNode* owner_at(const Cst& cst, TextOffset offset);

}  // namespace cursiv
