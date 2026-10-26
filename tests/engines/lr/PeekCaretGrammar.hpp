#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/VirtualStateGrammar.hpp"

namespace tpeekcaret {
struct Sym : cursiv::Extends<cursiv::AstNode> {};
struct Alpha : cursiv::Extends<Sym> {};
struct Beta : cursiv::Extends<Sym> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view a, b;
  reference<Sym> left, right;
};
struct G : cursiv::GrammarSpec {
  struct AT : terminal<lit<"@">> {};
  struct KEY : terminal<kw<"t">> {};
  struct RAW : terminal<lit<"t">> {};
  struct A : rule<assign<&Doc::a>(ref<AT>)> {};
  struct B : rule<assign<&Doc::b>(ref<AT>)> {};
  // The words after RAW split by arm and not by caller: the peek lexes its
  // second token in one mode for both callers.
  struct Root
      : rule<(lit<"p"> + ref<A> + ref<KEY> + lit<"#"> + lit<"!">) |
             (lit<"p"> + ref<B> + ref<KEY> + lit<"$"> + lit<"!">) |
             (lit<"q"> + ref<A> + ref<KEY> + lit<"#"> + lit<"?">) |
             (lit<"q"> + ref<B> + ref<KEY> + lit<"$"> + lit<"?">) |
             (lit<"p"> + ref<A> + ref<RAW> +
              assign<cursiv::xref<&Doc::left, Alpha>>(lit<"x"> | lit<"z">) +
              lit<"!">) |
             (lit<"p"> + ref<B> + ref<RAW> +
              assign<cursiv::xref<&Doc::right, Beta>>(lit<"y"> | lit<"w">) +
              lit<"!">) |
             (lit<"q"> + ref<A> + ref<RAW> +
              assign<cursiv::xref<&Doc::left, Alpha>>(lit<"x"> | lit<"z">) +
              lit<"?">) |
             (lit<"q"> + ref<B> + ref<RAW> +
              assign<cursiv::xref<&Doc::right, Beta>>(lit<"y"> | lit<"w">) +
              lit<"?">)> {};
  // Beside the peeks, the grammars of VirtualStateGrammar.hpp, which read no
  // trivia either, each behind a mark its text follows with no gap: no rule
  // is shared, so the two peeks stay the table's only ones.
  struct Top : rule<ref<Root> | (lit<"virtual:"> + ref<tvirtual::G::Root>) |
                    (lit<"reduce:"> + ref<treducecells::G::Root>)> {};
  using entry = Top;
};

}  // namespace tpeekcaret
