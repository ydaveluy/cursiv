#pragma once

// Where the closers of a text that ends open are inserted: a nest whose closer
// stands after forty words its state offers first, so forty attempts come
// before each closer that is owed; a grammar whose shortest completion is as
// long as its rules say -- 2^63 tokens after an `x`; and a ladder whose every
// level reads the one below as itself or through a rule of its own, so the
// table forks four times over the closer of a group.

#include <cstddef>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tdegraded {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Nest : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Item;
  struct Group : rule<lit<"("> + many(ref<Item>) + lit<")">> {};
  struct Item
      : rule<kw<"w0"> | kw<"w1"> | kw<"w2"> | kw<"w3"> | kw<"w4"> | kw<"w5"> |
             kw<"w6"> | kw<"w7"> | kw<"w8"> | kw<"w9"> | kw<"w10"> | kw<"w11"> |
             kw<"w12"> | kw<"w13"> | kw<"w14"> | kw<"w15"> | kw<"w16"> |
             kw<"w17"> | kw<"w18"> | kw<"w19"> | kw<"w20"> | kw<"w21"> |
             kw<"w22"> | kw<"w23"> | kw<"w24"> | kw<"w25"> | kw<"w26"> |
             kw<"w27"> | kw<"w28"> | kw<"w29"> | kw<"w30"> | kw<"w31"> |
             kw<"w32"> | kw<"w33"> | kw<"w34"> | kw<"w35"> | kw<"w36"> |
             kw<"w37"> | kw<"w38"> | kw<"w39"> | ref<Group>> {};
  struct Root : rule<create<ast::Node>(many(ref<Item>))> {};
  using entry = Root;
  using ignore = WS;
};

struct Deep : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  template <std::size_t K>
  struct A : rule<ref<A<K - 1>> + ref<A<K - 1>>> {};
  struct S : rule<create<ast::Node>(ref<A<63>>)> {};
  using entry = S;
  using ignore = WS;
};
template <>
struct Deep::A<0> : Deep::rule<lit<"x">> {};

namespace ladder {
struct Tree : cursiv::Extends<cursiv::AstNode> {
  vector<Tree*> kids;
};
struct Group : cursiv::Extends<Tree> {};
struct Word : cursiv::Extends<Tree> {};
struct Direct : cursiv::Extends<Tree> {};
struct Through : cursiv::Extends<Tree> {};
struct Via : cursiv::Extends<Tree> {};
struct Doc : cursiv::Extends<Tree> {};
}  // namespace ladder

struct Ladder : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Item;
  struct L0 : rule<create<ladder::Group>(
                  lit<"("> + many(append<&ladder::Tree::kids>(ref<Item>)) +
                  lit<")">)> {};
  struct V0 : rule<create<ladder::Via>(append<&ladder::Tree::kids>(ref<L0>))> {
  };
  struct L1
      : rule<create<ladder::Direct>(append<&ladder::Tree::kids>(ref<L0>)) |
             create<ladder::Through>(append<&ladder::Tree::kids>(ref<V0>))> {};
  struct V1 : rule<create<ladder::Via>(append<&ladder::Tree::kids>(ref<L1>))> {
  };
  struct L2
      : rule<create<ladder::Direct>(append<&ladder::Tree::kids>(ref<L1>)) |
             create<ladder::Through>(append<&ladder::Tree::kids>(ref<V1>))> {};
  struct V2 : rule<create<ladder::Via>(append<&ladder::Tree::kids>(ref<L2>))> {
  };
  struct L3
      : rule<create<ladder::Direct>(append<&ladder::Tree::kids>(ref<L2>)) |
             create<ladder::Through>(append<&ladder::Tree::kids>(ref<V2>))> {};
  struct V3 : rule<create<ladder::Via>(append<&ladder::Tree::kids>(ref<L3>))> {
  };
  struct L4
      : rule<create<ladder::Direct>(append<&ladder::Tree::kids>(ref<L3>)) |
             create<ladder::Through>(append<&ladder::Tree::kids>(ref<V3>))> {};
  struct Item : rule<ref<L4> | create<ladder::Word>(kw<"w">)> {};
  struct Root : rule<create<ladder::Doc>(
                    many(append<&ladder::Tree::kids>(ref<Item>)))> {};
  using entry = Root;
  using ignore = WS;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<(kw<"nest"> + ref<Nest::Root>) |
                     (kw<"deep"> + ref<Deep::S>) |
                     (kw<"ladder"> + ref<Ladder::Root>)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tdegraded
