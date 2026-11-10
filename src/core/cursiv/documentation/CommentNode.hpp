// The comment written above a node as a place in the document, for callers that
// need its position and not only its text.
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

// Parses comment record `comment`. Its ranges are document ranges: `offset`
// can feed a diagnostic's range.
[[nodiscard]] DocComment parse_doc_comment(
    const Document& document, NodeId comment, const DocCommentParseOptions& options = {});

}  // namespace cursiv
