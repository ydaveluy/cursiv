#pragma once

// Two readings of `c a` that part only below a left corner of twenty rungs:
// one node over both tokens, or `c` and a node over `a`. Each rung lays one
// word, so the corner's last word is all that orders the readings.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcornerkey {

struct Item : cursiv::AstNode {};
struct Whole : cursiv::Extends<Item> {};
struct Parted : cursiv::Extends<Item> {};
struct Doc : cursiv::AstNode {
  Item* item = nullptr;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct Whole_ : rule<create<Whole>(kw<"c"> + kw<"d">) |
                       create<Whole>(kw<"c"> + kw<"a">)> {};
  struct Tail : rule<kw<"a"> | kw<"e">> {};
  struct Parted_ : rule<create<Parted>(kw<"c"> + ref<Tail>)> {};
  struct Fork : rule<ref<Whole_> | ref<Parted_>> {};
  struct R19 : rule<ref<Fork> | create<Item>(kw<"w19">)> {};
  struct R18 : rule<ref<R19> | create<Item>(kw<"w18">)> {};
  struct R17 : rule<ref<R18> | create<Item>(kw<"w17">)> {};
  struct R16 : rule<ref<R17> | create<Item>(kw<"w16">)> {};
  struct R15 : rule<ref<R16> | create<Item>(kw<"w15">)> {};
  struct R14 : rule<ref<R15> | create<Item>(kw<"w14">)> {};
  struct R13 : rule<ref<R14> | create<Item>(kw<"w13">)> {};
  struct R12 : rule<ref<R13> | create<Item>(kw<"w12">)> {};
  struct R11 : rule<ref<R12> | create<Item>(kw<"w11">)> {};
  struct R10 : rule<ref<R11> | create<Item>(kw<"w10">)> {};
  struct R9 : rule<ref<R10> | create<Item>(kw<"w9">)> {};
  struct R8 : rule<ref<R9> | create<Item>(kw<"w8">)> {};
  struct R7 : rule<ref<R8> | create<Item>(kw<"w7">)> {};
  struct R6 : rule<ref<R7> | create<Item>(kw<"w6">)> {};
  struct R5 : rule<ref<R6> | create<Item>(kw<"w5">)> {};
  struct R4 : rule<ref<R5> | create<Item>(kw<"w4">)> {};
  struct R3 : rule<ref<R4> | create<Item>(kw<"w3">)> {};
  struct R2 : rule<ref<R3> | create<Item>(kw<"w2">)> {};
  struct R1 : rule<ref<R2> | create<Item>(kw<"w1">)> {};
  struct R0 : rule<ref<R1> | create<Item>(kw<"w0">)> {};
  struct Root : rule<create<Doc>(assign<&Doc::item>(ref<R0>) + kw<"x">) + eof> {
  };
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tcornerkey
