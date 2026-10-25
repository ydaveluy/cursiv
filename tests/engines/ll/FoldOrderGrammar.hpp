#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tfoldorder {

struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Leaf : cursiv::Extends<Node> {};
struct Pair : cursiv::Extends<Node> {};
struct Tail : cursiv::Extends<Node> {};
struct Turn : cursiv::Extends<Node> {
  Node* inner{};
  Tail* tail{};
  vector<Tail*> tails;
};
struct Short : cursiv::Extends<Node> { Node* inner{}; };
struct Long : cursiv::Extends<Node> { Node* inner{}; };
struct Neg : cursiv::Extends<Node> { Node* inner{}; };
struct Post : cursiv::Extends<Node> { Node* inner{}; };
struct Doc : cursiv::Extends<cursiv::AstNode> { Node* value{}; };

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};

  struct TurnFirst
      : rule<create<Turn>(assign<&Turn::inner>(ref<TurnFirst>) + lit<"x">) |
             create<Pair>(lit<"a"> + lit<"x">) | create<Leaf>(lit<"a">)> {};
  struct BaseFirst
      : rule<create<Pair>(lit<"a"> + lit<"x">) |
             create<Turn>(assign<&Turn::inner>(ref<BaseFirst>) + lit<"x">) |
             create<Leaf>(lit<"a">)> {};
  struct TurnLast
      : rule<create<Pair>(lit<"a"> + lit<"x">) | create<Leaf>(lit<"a">) |
             create<Turn>(assign<&Turn::inner>(ref<TurnLast>) + lit<"x">)> {};

  struct LongFirst
      : rule<create<Long>(assign<&Long::inner>(ref<LongFirst>) + lit<","> +
                          lit<"a"> + lit<","> + lit<"a">) |
             create<Short>(assign<&Short::inner>(ref<LongFirst>) + lit<","> +
                           lit<"a">) |
             create<Leaf>(lit<"a">)> {};
  struct ShortFirst
      : rule<create<Short>(assign<&Short::inner>(ref<ShortFirst>) + lit<","> +
                           lit<"a">) |
             create<Long>(assign<&Long::inner>(ref<ShortFirst>) + lit<","> +
                          lit<"a"> + lit<","> + lit<"a">) |
             create<Leaf>(lit<"a">)> {};

  struct PrefixFirst
      : rule<create<Neg>(lit<"-"> + assign<&Neg::inner>(ref<PrefixFirst>)) |
             create<Post>(assign<&Post::inner>(ref<PrefixFirst>) + lit<"!">) |
             create<Leaf>(lit<"a">)> {};
  struct PostfixFirst
      : rule<create<Post>(assign<&Post::inner>(ref<PostfixFirst>) + lit<"!">) |
             create<Neg>(lit<"-"> + assign<&Neg::inner>(ref<PostfixFirst>)) |
             create<Leaf>(lit<"a">)> {};

  struct OneTail : rule<create<Tail>(lit<"x">)> {};
  struct TailTurnFirst
      : rule<create<Turn>(assign<&Turn::inner>(ref<TailTurnFirst>) + lit<"x"> +
                          option(assign<&Turn::tail>(ref<OneTail>))) |
             create<Leaf>(lit<"a">)> {};
  struct TailBaseFirst
      : rule<create<Leaf>(lit<"a">) |
             create<Turn>(assign<&Turn::inner>(ref<TailBaseFirst>) + lit<"x"> +
                          option(assign<&Turn::tail>(ref<OneTail>)))> {};

  struct ManyTurnFirst
      : rule<create<Turn>(assign<&Turn::inner>(ref<ManyTurnFirst>) + lit<"x"> +
                          many(append<&Turn::tails>(ref<OneTail>))) |
             create<Leaf>(lit<"a">)> {};
  struct ManyBaseFirst
      : rule<create<Leaf>(lit<"a">) |
             create<Turn>(assign<&Turn::inner>(ref<ManyBaseFirst>) + lit<"x"> +
                          many(append<&Turn::tails>(ref<OneTail>)))> {};

  struct ViaFirst;
  struct ViaFirstTurn
      : rule<create<Turn>(assign<&Turn::inner>(ref<ViaFirst>) + lit<"x">)> {};
  struct ViaFirst
      : rule<ref<ViaFirstTurn> | create<Pair>(lit<"a"> + lit<"x">) |
             create<Leaf>(lit<"a">)> {};
  struct ViaLast;
  struct ViaLastTurn
      : rule<create<Turn>(assign<&Turn::inner>(ref<ViaLast>) + lit<"x">)> {};
  struct ViaLast
      : rule<create<Pair>(lit<"a"> + lit<"x">) | create<Leaf>(lit<"a">) |
             ref<ViaLastTurn>> {};

  struct Root
      : rule<create<Doc>(
                 (lit<"s1"> + assign<&Doc::value>(ref<TurnFirst>)) |
                 (lit<"s2"> + assign<&Doc::value>(ref<BaseFirst>)) |
                 (lit<"s3"> + assign<&Doc::value>(ref<TurnLast>)) |
                 (lit<"t1"> + assign<&Doc::value>(ref<LongFirst>)) |
                 (lit<"t2"> + assign<&Doc::value>(ref<ShortFirst>)) |
                 (lit<"p1"> + assign<&Doc::value>(ref<PrefixFirst>)) |
                 (lit<"p2"> + assign<&Doc::value>(ref<PostfixFirst>)) |
                 (lit<"o1"> + assign<&Doc::value>(ref<TailTurnFirst>)) |
                 (lit<"o2"> + assign<&Doc::value>(ref<TailBaseFirst>)) |
                 (lit<"m1"> + assign<&Doc::value>(ref<ManyTurnFirst>)) |
                 (lit<"m2"> + assign<&Doc::value>(ref<ManyBaseFirst>)) |
                 (lit<"i1"> + assign<&Doc::value>(ref<ViaFirst>)) |
                 (lit<"i2"> + assign<&Doc::value>(ref<ViaLast>))) +
             eof> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tfoldorder
