#pragma once

// A chain of twenty-six unit reductions that two tokens share: the state
// after `x` reduces by one production on `a` and on `b`, and so does each
// state above it, up to `S`, which writes `b` before `a`. `x` is read first,
// so its state is the first the construction orders and walks the whole chain.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tchaininsert {

struct Item : cursiv::AstNode {};
struct Doc : cursiv::AstNode {
  Item* item = nullptr;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct E26 : rule<create<Item>(kw<"x">) | create<Item>(kw<"v">)> {};
  struct E25 : rule<ref<E26> | create<Item>(kw<"u25">)> {};
  struct E24 : rule<ref<E25> | create<Item>(kw<"u24">)> {};
  struct E23 : rule<ref<E24> | create<Item>(kw<"u23">)> {};
  struct E22 : rule<ref<E23> | create<Item>(kw<"u22">)> {};
  struct E21 : rule<ref<E22> | create<Item>(kw<"u21">)> {};
  struct E20 : rule<ref<E21> | create<Item>(kw<"u20">)> {};
  struct E19 : rule<ref<E20> | create<Item>(kw<"u19">)> {};
  struct E18 : rule<ref<E19> | create<Item>(kw<"u18">)> {};
  struct E17 : rule<ref<E18> | create<Item>(kw<"u17">)> {};
  struct E16 : rule<ref<E17> | create<Item>(kw<"u16">)> {};
  struct E15 : rule<ref<E16> | create<Item>(kw<"u15">)> {};
  struct E14 : rule<ref<E15> | create<Item>(kw<"u14">)> {};
  struct E13 : rule<ref<E14> | create<Item>(kw<"u13">)> {};
  struct E12 : rule<ref<E13> | create<Item>(kw<"u12">)> {};
  struct E11 : rule<ref<E12> | create<Item>(kw<"u11">)> {};
  struct E10 : rule<ref<E11> | create<Item>(kw<"u10">)> {};
  struct E9 : rule<ref<E10> | create<Item>(kw<"u9">)> {};
  struct E8 : rule<ref<E9> | create<Item>(kw<"u8">)> {};
  struct E7 : rule<ref<E8> | create<Item>(kw<"u7">)> {};
  struct E6 : rule<ref<E7> | create<Item>(kw<"u6">)> {};
  struct E5 : rule<ref<E6> | create<Item>(kw<"u5">)> {};
  struct E4 : rule<ref<E5> | create<Item>(kw<"u4">)> {};
  struct E3 : rule<ref<E4> | create<Item>(kw<"u3">)> {};
  struct E2 : rule<ref<E3> | create<Item>(kw<"u2">)> {};
  struct E1 : rule<ref<E2> | create<Item>(kw<"u1">)> {};
  struct E0 : rule<ref<E1> | create<Item>(kw<"u0">)> {};
  struct S : rule<create<Doc>(assign<&Doc::item>(ref<E0>) + kw<"b">) |
                  create<Doc>(assign<&Doc::item>(ref<E0>) + kw<"a">)> {};
  struct T : rule<create<Doc>(assign<&Doc::item>(ref<E0>) + kw<"c">)> {};
  struct Root : rule<create<Doc>(kw<"x"> + kw<"c">) |
                     create<Doc>(kw<"a"> + kw<"q">) | ref<S> | ref<T>> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tchaininsert
