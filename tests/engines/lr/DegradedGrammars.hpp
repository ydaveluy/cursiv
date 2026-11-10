#pragma once

// Where the closers of a text that ends open are inserted: a nest whose closer
// comes after forty words its state offers first, a grammar whose shortest
// completion is 2^63 tokens, and a chain of rules the table forks four times
// over.

#include <cstddef>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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
  using ignored = WS;
};

struct Deep : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  template <std::size_t K>
  struct A : rule<ref<A<K - 1>> + ref<A<K - 1>>> {};
  struct S : rule<create<ast::Node>(ref<A<63>>)> {};
  using entry = S;
  using ignored = WS;
};
template <>
struct Deep::A<0> : Deep::rule<lit<"x">> {};

namespace forked {
struct Tree : cursiv::Extends<cursiv::AstNode> {
  vector<Tree*> children;
};
struct Group : cursiv::Extends<Tree> {};
struct Word : cursiv::Extends<Tree> {};
struct Direct : cursiv::Extends<Tree> {};
struct Through : cursiv::Extends<Tree> {};
struct Via : cursiv::Extends<Tree> {};
struct Doc : cursiv::Extends<Tree> {};
}  // namespace forked

struct Forked : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Item;
  struct L0 : rule<create<forked::Group>(
                  lit<"("> + many(append<&forked::Tree::children>(ref<Item>)) +
                  lit<")">)> {};
  struct V0
      : rule<create<forked::Via>(append<&forked::Tree::children>(ref<L0>))> {};
  struct L1
      : rule<create<forked::Direct>(append<&forked::Tree::children>(ref<L0>)) |
             create<forked::Through>(
                 append<&forked::Tree::children>(ref<V0>))> {};
  struct V1
      : rule<create<forked::Via>(append<&forked::Tree::children>(ref<L1>))> {};
  struct L2
      : rule<create<forked::Direct>(append<&forked::Tree::children>(ref<L1>)) |
             create<forked::Through>(
                 append<&forked::Tree::children>(ref<V1>))> {};
  struct V2
      : rule<create<forked::Via>(append<&forked::Tree::children>(ref<L2>))> {};
  struct L3
      : rule<create<forked::Direct>(append<&forked::Tree::children>(ref<L2>)) |
             create<forked::Through>(
                 append<&forked::Tree::children>(ref<V2>))> {};
  struct V3
      : rule<create<forked::Via>(append<&forked::Tree::children>(ref<L3>))> {};
  struct L4
      : rule<create<forked::Direct>(append<&forked::Tree::children>(ref<L3>)) |
             create<forked::Through>(
                 append<&forked::Tree::children>(ref<V3>))> {};
  struct Item : rule<ref<L4> | create<forked::Word>(kw<"w">)> {};
  struct Root : rule<create<forked::Doc>(
                    many(append<&forked::Tree::children>(ref<Item>)))> {};
  using entry = Root;
  using ignored = WS;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<(kw<"nest"> + ref<Nest::Root>) | (kw<"deep"> + ref<Deep::S>) |
             (kw<"forked"> + ref<Forked::Root>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tdegraded
