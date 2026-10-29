#pragma once

// Tokens that cycle through states which accept nothing and loop on no byte,
// one of them shifted by a byte from the next, and one whose acceptance a word
// after it refuses: text no token starts in, read by walks that go far.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcycle {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct T : terminal<some(lit<"ab">) + lit<"c">> {};
  struct V : terminal<some(lit<"dd">) + lit<"e">> {};
  struct U : terminal<lit<"xy", !cr<"z">>> {};
  struct Root : rule<create<ast::Doc>(
                    many(append<&ast::Doc::items>(ref<T> | ref<V> | ref<U>)))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace tcycle
