#pragma once

// A cell after `x` on `a` that reduce or shift settle only at the token that
// ends the statement, however long the run of `a` before it.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tepisode {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Long : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct C : rule<lit<"x">> {};
  struct Stat : rule<(ref<C> + many(lit<"a">) + lit<"b">) |
                     (lit<"x"> + many(lit<"a">) + lit<"c">) |
                     (lit<"{"> + many(ref<Stat>) + lit<"}">)> {};
  struct Root : rule<create<ast::Doc>(many(ref<Stat>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tepisode
