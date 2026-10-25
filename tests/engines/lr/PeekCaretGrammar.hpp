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

struct Splice : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Tail : terminal<some(lit<"c">) + lit<"d">> {};
  struct Merged : terminal<lit<"ab"> + some(lit<"c">) + lit<"d">> {};
  struct Guarded : terminal<lit<"ab"> + !lit<"c">> {};
  struct BeforeB : terminal<lit<"a"> + !lit<"b">> {};
  struct SameWidth : terminal<lit<"a">> {};
  // The same two-symbol reduction in different callers. Pair writes no
  // record: completion cannot recover this return by walking CST closes.
  struct PairHead : terminal<lit<"a">> {};
  struct PairTail : terminal<lit<"b">> {};
  struct Pair : rule<ref<PairHead> + ref<PairTail>> {};
  struct Root : rule<create<Doc>(
      (lit<"("> + lit<"foo"> + lit<")">) |
      (lit<"{"> + lit<"a"> + option(lit<"b">) + lit<"c"> + lit<"}">) |
      (lit<"{"> + ref<Guarded> + lit<"!"> + lit<"}">) |
      (lit<"["> + lit<"a"> + option(lit<"b">) + ref<Tail> + lit<"]">) |
      (lit<"["> + ref<Merged> + lit<"!"> + lit<"]">) |
      (lit<"<"> + ref<BeforeB> + option(lit<"b">) + lit<"c"> + lit<">">) |
      (lit<"<"> + ref<SameWidth> + lit<"d"> + lit<">">) |
      (lit<"x"> + ref<Pair> + lit<"!">) |
      (lit<"y"> + ref<Pair> + lit<"?">) |
      (lit<"u"> + many(lit<"("> + lit<"a"> + lit<"b"> + lit<")">) +
       lit<"!"> + option(lit<"next">)))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace tpeekcaret
