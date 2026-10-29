#pragma once

// A run that grows by turns: one takes a run of `a`, another takes the rest
// of them and a `b`. Which turn takes an `a` is the election's, and what the
// table lays first for it depends on how the text ends.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace ttableorder {
namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Run : cursiv::Extends<Node> {
  vector<Node*> body;
};
struct Doc : cursiv::Extends<Node> {
  vector<Node*> body;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct RunRule;
  struct RunRule : rule<create<ast::Run>(lit<"a">) |
                        create<ast::Run>(append<&ast::Run::body>(ref<RunRule>) +
                                         some(lit<"a">)) |
                        create<ast::Run>(append<&ast::Run::body>(ref<RunRule>) +
                                         many(lit<"a">) + lit<"b">)> {};
  struct DocRule
      : rule<create<ast::Doc>(option(lit<"a">) +
                              append<&ast::Doc::body>(ref<RunRule>)) |
             create<ast::Doc>(append<&ast::Doc::body>(ref<RunRule>) + lit<"b"> +
                              lit<"c">)> {};
  using entry = DocRule;
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace ttableorder
