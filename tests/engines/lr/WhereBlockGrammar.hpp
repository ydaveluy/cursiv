#pragma once

// Equations whose `where` block has no end marker: the next equation is the
// block's or any enclosing one's, and a pattern's `=` is an operator too, so
// the text never says where a block ends. `Open` leaves it to the election;
// `Taken` commits each block to an equation wherever one can be read, which is
// the reading `Open` elects.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace twhere {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Equations : cursiv::GrammarSpec {
  static constexpr auto Sym = cr<"=">;
  struct WS : terminal<some(cr<R"( \t\r\n)">)> {};
  struct ID : terminal<cr<"a-z"> + many(cr<"a-z0-9_">)> {};
  struct OP : terminal<some(Sym)> {};
  static constexpr auto Assign = lit<"=", !Sym>;
  struct Pat : rule<some(ref<ID>) + option(ref<OP> + ref<Pat>)> {};
  struct Term : rule<some(ref<ID>)> {};
  struct IfPart : rule<ref<Term> + lit<","> + ref<Term>> {};
  struct Rhs : rule<ref<Term> | (ref<IfPart> + many(Assign + ref<IfPart>))> {};
  using ignored = WS;
};

struct Open : Equations {
  struct Rest : rule<many(ref<ID>) + option(ref<OP> + ref<Pat>) + Assign +
                     ref<Rhs> +
                     option(kw<"where"> + some(ref<ID> + ref<Rest>))> {};
  struct Root : rule<create<ast::Doc>(some(ref<ID> + ref<Rest>))> {};
  using entry = Root;
};

struct Taken : Equations {
  struct Rest : rule<many(ref<ID>) + option(ref<OP> + ref<Pat>) + Assign +
                     ref<Rhs> +
                     option(kw<"where"> +
                            some(commit(ref<ID>, ref<Rest>)))> {};
  struct Root : rule<create<ast::Doc>(some(ref<ID> + ref<Rest>))> {};
  using entry = Root;
};

}  // namespace twhere
