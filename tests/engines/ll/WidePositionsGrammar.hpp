#pragma once

// A compact lexer and parser for the wide-position runtime test. Its long
// linear program is constructed as table data in LlWidePositions_test.cpp;
// emitting every position as parser code adds no runtime coverage.

#include <cstddef>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace twidepos {

namespace ast {
struct Document : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

inline constexpr std::size_t kLeading = 16500;

struct Ll : cursiv::GrammarSpec {
  struct Root : rule<create<ast::Document>(some(lit<"a">) + lit<"b"> + lit<"c">)> {};
  using entry = Root;
};

}  // namespace twidepos
