// A repetition whose turn may read nothing, each item optional: the sentence
// still shows the rule as its author wrote it.
// expect LR LL: refused .nullableRepetition..*rule .X. repeats an item that may match nothing.*X returns Pair
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Pair : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> name;
  vector<string_view> other;
};
}  // namespace ast

struct G : Base {
  struct X : rule<create<ast::Pair>(
                 kw<"go"> +
                 many(option(append<&ast::Pair::name>(lit<"a">)) +
                      option(append<&ast::Pair::other>(lit<"b">))) +
                 lit<";">)> {};
  using entry = X;
  using ignored = WS;
};

}  // namespace refuse
