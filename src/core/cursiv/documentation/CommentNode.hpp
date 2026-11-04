// The comment written above a node as a place in the document, for what needs
// its position and not only its text.
#pragma once

#include <span>
#include <string>

#include "cursiv/documentation/DocComment.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"

namespace cursiv {

struct Document;

// The record of one of the terminal rules `ruleNames` standing right before
// `node`, or `kNoNode`. The root reads its whole leading trivia. Read its text
// and range from the document's CST.
[[nodiscard]] NodeId find_comment_node(const Document& document,
                                       const AstNode& node,
                                       std::span<const std::string> ruleNames);

// The documentation in comment record `comment`, its ranges those of the
// document: `offset` is what a diagnostic's range takes.
[[nodiscard]] DocComment parse_doc_comment(
    const Cst& cst, NodeId comment, const DocCommentParseOptions& options = {});

}  // namespace cursiv
