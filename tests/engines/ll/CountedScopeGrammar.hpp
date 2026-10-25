#pragma once

// Counted repetitions beside, inside and under directives, ahead of and
// behind positions that call a rule: each rule reads as one function of the
// descent, whatever labels it holds, and the table engine reads the same text
// the same way.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// Two runs, each of a kind, a token, a floor and a ceiling: the kinds P, S and
// G read no rule, C and X call one.
#define TSCOPE_PAIRS(ROW)              \
  ROW(k00, P, 'a', 0, 3, C, 'b', 0, 3) \
  ROW(k01, P, 'a', 0, 3, C, 'b', 2, 4) \
  ROW(k02, P, 'a', 0, 3, C, 'b', 0, 1) \
  ROW(k03, P, 'a', 0, 3, C, 'b', 2, 3) \
  ROW(k04, P, 'a', 0, 3, C, 'b', 1, 2) \
  ROW(k05, P, 'a', 2, 4, C, 'b', 0, 3) \
  ROW(k06, P, 'a', 2, 4, C, 'b', 2, 4) \
  ROW(k07, P, 'a', 2, 4, C, 'b', 0, 1) \
  ROW(k08, P, 'a', 2, 4, C, 'b', 2, 3) \
  ROW(k09, P, 'a', 2, 4, C, 'b', 1, 2) \
  ROW(k10, P, 'a', 0, 1, C, 'b', 0, 3) \
  ROW(k11, P, 'a', 0, 1, C, 'b', 2, 4) \
  ROW(k12, P, 'a', 0, 1, C, 'b', 0, 1) \
  ROW(k13, P, 'a', 0, 1, C, 'b', 2, 3) \
  ROW(k14, P, 'a', 0, 1, C, 'b', 1, 2) \
  ROW(k15, P, 'a', 2, 3, C, 'b', 0, 3) \
  ROW(k16, P, 'a', 2, 3, C, 'b', 2, 4) \
  ROW(k17, P, 'a', 2, 3, C, 'b', 0, 1) \
  ROW(k18, P, 'a', 2, 3, C, 'b', 2, 3) \
  ROW(k19, P, 'a', 2, 3, C, 'b', 1, 2) \
  ROW(k20, P, 'a', 1, 2, C, 'b', 0, 3) \
  ROW(k21, P, 'a', 1, 2, C, 'b', 2, 4) \
  ROW(k22, P, 'a', 1, 2, C, 'b', 0, 1) \
  ROW(k23, P, 'a', 1, 2, C, 'b', 2, 3) \
  ROW(k24, P, 'a', 1, 2, C, 'b', 1, 2) \
  ROW(k25, P, 'a', 0, 3, C, 'b', 2, 4) \
  ROW(k26, C, 'b', 0, 3, P, 'a', 2, 4) \
  ROW(k27, P, 'a', 2, 4, C, 'b', 0, 3) \
  ROW(k28, C, 'b', 2, 4, P, 'a', 0, 3) \
  ROW(k29, P, 'a', 0, 3, X, 'b', 2, 4) \
  ROW(k30, X, 'b', 0, 3, P, 'a', 2, 4) \
  ROW(k31, P, 'a', 2, 4, X, 'b', 0, 3) \
  ROW(k32, X, 'b', 2, 4, P, 'a', 0, 3) \
  ROW(k33, S, 'a', 0, 3, C, 'b', 2, 4) \
  ROW(k34, C, 'b', 0, 3, S, 'a', 2, 4) \
  ROW(k35, S, 'a', 2, 4, C, 'b', 0, 3) \
  ROW(k36, C, 'b', 2, 4, S, 'a', 0, 3) \
  ROW(k37, S, 'a', 0, 3, X, 'b', 2, 4) \
  ROW(k38, X, 'b', 0, 3, S, 'a', 2, 4) \
  ROW(k39, S, 'a', 2, 4, X, 'b', 0, 3) \
  ROW(k40, X, 'b', 2, 4, S, 'a', 0, 3) \
  ROW(k41, G, 'a', 0, 3, C, 'b', 2, 4) \
  ROW(k42, C, 'b', 0, 3, G, 'a', 2, 4) \
  ROW(k43, G, 'a', 2, 4, C, 'b', 0, 3) \
  ROW(k44, C, 'b', 2, 4, G, 'a', 0, 3) \
  ROW(k45, G, 'a', 0, 3, X, 'b', 2, 4) \
  ROW(k46, X, 'b', 0, 3, G, 'a', 2, 4) \
  ROW(k47, G, 'a', 2, 4, X, 'b', 0, 3) \
  ROW(k48, X, 'b', 2, 4, G, 'a', 0, 3) \
  ROW(k49, C, 'b', 0, 3, P, 'a', 0, 3) \
  ROW(k50, C, 'b', 2, 4, P, 'a', 2, 4) \
  ROW(k51, C, 'b', 0, 1, P, 'a', 0, 1) \
  ROW(k52, C, 'b', 2, 3, P, 'a', 2, 3) \
  ROW(k53, C, 'b', 1, 2, P, 'a', 1, 2)

#define TSCOPE_P append<&ast::Doc::p>(ref<A>)
#define TSCOPE_S append<&ast::Doc::p>(lit<"a"> | lit<"x">)
#define TSCOPE_G \
  append<&ast::Doc::r>(create<ast::Item>(assign<&ast::Item::text>(ref<A>)))
#define TSCOPE_C append<&ast::Doc::r>(ref<ItemB>)
#define TSCOPE_X append<&ast::Doc::r>(ref<ItB>)

#define TSCOPE_ROW(KEY, K1, T1, LO1, HI1, K2, T2, LO2, HI2)    \
  struct KEY : rule<kw<#KEY> + repeat<LO1, HI1>(TSCOPE_##K1) + \
                    repeat<LO2, HI2>(TSCOPE_##K2)> {};
#define TSCOPE_REF(KEY, ...) | ref<KEY>

namespace tscope {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Sub : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> p;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> p;
  vector<Item*> r;
  vector<Sub*> subs;
};
}  // namespace ast

struct Inner : cursiv::GrammarSpec {
  struct Root : rule<lit<"i"> + lit<";">> {};
  using entry = Root;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A : terminal<lit<"a">> {};
  struct B : terminal<lit<"b">> {};
  struct D : terminal<lit<"d">> {};
  struct ItemB : rule<create<ast::Item>(assign<&ast::Item::text>(ref<B>))> {};
  struct ItemC : rule<create<ast::Item>(assign<&ast::Item::text>(lit<"c">))> {};
  struct ItB : rule<ref<ItemB> | ref<ItemC>> {};
  struct Run : rule<repeat<0, 3>(TSCOPE_P)> {};

  TSCOPE_PAIRS(TSCOPE_ROW)

  struct Nested
      : rule<kw<"n01"> +
             repeat<0, 2>(TSCOPE_P +
                          repeat<1, 2>(append<&ast::Doc::p>(ref<D>))) +
             repeat<1, 2>(TSCOPE_C)> {};
  struct Optioned
      : rule<kw<"o01"> +
             option(TSCOPE_P + repeat<0, 3>(append<&ast::Doc::p>(ref<D>))) +
             repeat<2, 4>(TSCOPE_X)> {};
  struct Looped : rule<kw<"m01"> + many(repeat<2, 3>(TSCOPE_P) + lit<";">) +
                       repeat<1, 2>(TSCOPE_C)> {};
  struct Arms : rule<kw<"a01"> + ((repeat<0, 3>(TSCOPE_P) + lit<"x">) |
                                  (repeat<2, 4>(TSCOPE_C) + lit<"y">))> {};
  struct Created : rule<kw<"c01"> +
                        append<&ast::Doc::subs>(create<ast::Sub>(
                            repeat<0, 3>(append<&ast::Sub::p>(ref<A>)))) +
                        repeat<2, 4>(TSCOPE_X)> {};
  struct Records : rule<kw<"d01"> +
                        repeat<0, 3>(append<&ast::Doc::subs>(
                            create<ast::Sub>(append<&ast::Sub::p>(ref<A>)))) +
                        repeat<2, 4>(TSCOPE_X)> {};
  struct Triple
      : rule<kw<"t01"> + repeat<0, 3>(TSCOPE_P) + repeat<1, 2>(TSCOPE_C) +
             repeat<0, 2>(append<&ast::Doc::p>(ref<D>))> {};
  struct Sectioned
      : rule<kw<"s01"> + repeat<0, 3>(TSCOPE_P) +
             section<Inner>(lit<"<">, lit<">">) + repeat<2, 4>(TSCOPE_X)> {};
  struct Called : rule<kw<"r01"> + ref<Run> + repeat<2, 4>(TSCOPE_X)> {};
  struct Tight : rule<kw<"g01"> + tight(repeat<0, 3>(TSCOPE_P) +
                                        repeat<2, 4>(TSCOPE_X) + lit<";">)> {};

  struct Root
      : rule<(ref<Nested> | ref<Optioned> | ref<Looped> | ref<Arms> |
              ref<Created> | ref<Records> | ref<Triple> | ref<Sectioned> |
              ref<Called> | ref<Tight> TSCOPE_PAIRS(TSCOPE_REF)) +
             eof> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tscope

#undef TSCOPE_REF
#undef TSCOPE_X
#undef TSCOPE_C
#undef TSCOPE_G
#undef TSCOPE_S
#undef TSCOPE_P
#undef TSCOPE_ROW
