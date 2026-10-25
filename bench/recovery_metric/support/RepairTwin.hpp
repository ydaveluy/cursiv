// A valid document read once, as every faulted copy of it is weighed: its
// text, its parse, and its tokens -- the visible leaves that carry bytes, in
// document order. Neither copyable nor movable: the parse borrows the text.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"

namespace cursiv {

struct RepairTwin {
  std::string text;
  ParseResult result;
  std::vector<CstLeaf> tokens;
  // Per token: spelled by the grammar rather than matched by a named rule.
  std::vector<bool> literal;
  // Trivia that carries bytes: comments, and blanks a grammar records.
  std::vector<CstLeaf> hidden;

  explicit RepairTwin(std::string t) : text(std::move(t)) {}
  RepairTwin(const RepairTwin&) = delete;
  RepairTwin& operator=(const RepairTwin&) = delete;

  // Null unless the parse takes the text clean and builds a tree: a twin
  // that needs a repair of its own measures nothing.
  [[nodiscard]] static std::unique_ptr<RepairTwin> read(const Parser& parser,
                                                        std::string text) {
    auto twin = std::make_unique<RepairTwin>(std::move(text));
    twin->result = parser.parse(twin->text);
    const Cst& cst = twin->result.cst();
    if (!cst.accepted() || cst.recovered() || twin->result.value == nullptr)
      return nullptr;
    const Grammar* grammar = cst.grammar();
    cst.visit_leaves([&](const CstLeaf& leaf) {
      if (leaf.begin == leaf.end) return;
      if (leaf.hidden) {
        twin->hidden.push_back(leaf);
        return;
      }
      twin->tokens.push_back(leaf);
      twin->literal.push_back(grammar == nullptr ||
                              grammar->recordRule(cst[leaf.id]).empty());
    });
    return twin;
  }

  [[nodiscard]] std::string_view spelling(std::size_t token) const {
    const CstLeaf& t = tokens[token];
    return std::string_view(text).substr(t.begin, t.end - t.begin);
  }
};

}  // namespace cursiv
