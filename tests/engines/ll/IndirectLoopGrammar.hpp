// Left recursion through other rules, entered by the rule a token names:
// `family/a` calls A of a copy nothing else calls, `family/ab:a` calls A of a
// copy that the document also reaches by B, A first. Cycle is what the descent
// refuses and the table reads; Shared, what both read: each of its cycles has
// an arm that only calls the next rule.
#pragma once

#include <algorithm>
#include <memory_resource>
#include <string_view>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tindirectloop {

struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Ba : cursiv::Extends<Node> {};
struct Bb : cursiv::Extends<Node> {};
struct Bc : cursiv::Extends<Node> {};
struct Bd : cursiv::Extends<Node> {};
struct Be : cursiv::Extends<Node> {};
struct Bax : cursiv::Extends<Node> {};
struct Bay : cursiv::Extends<Node> {};
struct Bby : cursiv::Extends<Node> {};
struct Tx : cursiv::Extends<Node> {
  Node* inner{};
};
struct Ty : cursiv::Extends<Node> {
  Node* inner{};
};
struct Tz : cursiv::Extends<Node> {
  Node* inner{};
};
struct Tw : cursiv::Extends<Node> {
  Node* inner{};
};
struct Tv : cursiv::Extends<Node> {
  Node* inner{};
};
struct Tq : cursiv::Extends<Node> {
  Node* inner{};
};
struct Tp : cursiv::Extends<Node> {
  Node* inner{};
};
struct Te : cursiv::Extends<Node> {
  Node* inner{};
};
struct Tr : cursiv::Extends<Node> {
  Node* inner{};
};
struct Neg : cursiv::Extends<Node> {
  Node* inner{};
};
struct Post : cursiv::Extends<Node> {
  Node* inner{};
};
struct Val : cursiv::Extends<Node> {
  std::string_view text;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Node* value{};
};

template <char C>
std::string_view wrapped(std::string_view text,
                         std::pmr::memory_resource* memory) {
  const auto room = cursiv::text_room(memory, text.size() + 3);
  room[0] = C;
  room[1] = '(';
  std::copy(text.begin(), text.end(), room.begin() + 2);
  room[room.size() - 1] = ')';
  return std::string_view(room.data(), room.size());
}
inline constexpr auto wrapA = [](std::string_view text,
                                 std::pmr::memory_resource* memory) {
  return wrapped<'A'>(text, memory);
};
inline constexpr auto wrapB = [](std::string_view text,
                                 std::pmr::memory_resource* memory) {
  return wrapped<'B'>(text, memory);
};

struct Common : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  using ignored = WS;
};

struct Cycle : Common {
  template <int>
  struct Mutual {
    struct B;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">) |
                    create<Ba>(lit<"a">)> {};
  };
  template <int>
  struct MutualA {
    struct B;
    struct A : rule<create<Bax>(lit<"a"> + lit<"x">) |
                    create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">) |
                    create<Ba>(lit<"a">)> {};
  };
  template <int>
  struct MutualB {
    struct B;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Ba>(lit<"a">) |
                    create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">)> {};
  };
  template <int>
  struct MutualAB {
    struct B;
    struct A : rule<create<Bax>(lit<"a"> + lit<"x">) |
                    create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">)> {};
    struct B : rule<create<Ba>(lit<"a">) |
                    create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">)> {};
  };

  template <int>
  struct Ring {
    struct B;
    struct C;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<C>) + lit<"y">) |
                    create<Bay>(lit<"a"> + lit<"y">)> {};
    struct C : rule<create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"z">) |
                    create<Bc>(lit<"a">)> {};
  };
  template <int>
  struct RingB {
    struct B;
    struct C;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Bay>(lit<"a"> + lit<"y">) |
                    create<Ty>(assign<&Ty::inner>(ref<C>) + lit<"y">)> {};
    struct C : rule<create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"z">) |
                    create<Bc>(lit<"a">)> {};
  };

  template <int>
  struct Rungs {
    struct B;
    struct C;
    struct D;
    struct A : rule<create<Tq>(assign<&Tq::inner>(ref<D>) + lit<"q">) |
                    create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Tw>(assign<&Tw::inner>(ref<C>) + lit<"w">) |
                    create<Tv>(assign<&Tv::inner>(ref<D>) + lit<"w">)> {};
    struct C : rule<create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"z">) |
                    create<Bc>(lit<"a">)> {};
    struct D : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">) |
                    create<Bd>(lit<"a">)> {};
  };
  template <int>
  struct RungsB {
    struct B;
    struct C;
    struct D;
    struct A : rule<create<Tq>(assign<&Tq::inner>(ref<D>) + lit<"q">) |
                    create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Tv>(assign<&Tv::inner>(ref<D>) + lit<"w">) |
                    create<Tw>(assign<&Tw::inner>(ref<C>) + lit<"w">)> {};
    struct C : rule<create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"z">) |
                    create<Bc>(lit<"a">)> {};
    struct D : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">) |
                    create<Bd>(lit<"a">)> {};
  };

  template <int>
  struct Share {
    struct D;
    struct E;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<D>) + lit<"x">) |
                    create<Tp>(assign<&Tp::inner>(ref<E>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct D : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">) |
                    create<Bd>(lit<"a">)> {};
    struct E : rule<create<Be>(lit<"a"> + lit<"e">) |
                    create<Te>(assign<&Te::inner>(ref<D>) + lit<"e">)> {};
  };

  template <int>
  struct Diamond {
    struct B;
    struct C;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Tz>(assign<&Tz::inner>(ref<C>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">)> {};
    struct C : rule<create<Tr>(assign<&Tr::inner>(ref<A>) + lit<"y">)> {};
  };
  template <int>
  struct DiamondS {
    struct B;
    struct C;
    struct A : rule<create<Tz>(assign<&Tz::inner>(ref<C>) + lit<"x">) |
                    create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">)> {};
    struct C : rule<create<Tr>(assign<&Tr::inner>(ref<A>) + lit<"y">)> {};
  };

  template <int>
  struct Paren {
    struct B;
    struct A
        : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
               create<Tp>(lit<"("> + assign<&Tp::inner>(ref<B>) + lit<")">) |
               create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">) |
                    create<Ba>(lit<"a">)> {};
  };

  template <int>
  struct Sub {
    struct B;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Ba>(lit<"a">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<B>) + lit<"y">) |
                    create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"z">) |
                    create<Bb>(lit<"b">)> {};
  };
  template <int>
  struct SubAmb {
    struct B;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Ba>(lit<"a">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<B>) + lit<"x">) |
                    create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"x">) |
                    create<Bb>(lit<"b">)> {};
  };

  template <int>
  struct Valued {
    struct B;
    struct A : rule<as<wrapA>((ref<B> + lit<"x">) | (lit<"a"> + lit<"x">))> {};
    struct B : rule<as<wrapB>((ref<A> + lit<"y">) | lit<"a">)> {};
    struct ReadA : rule<create<Val>(assign<&Val::text>(ref<A>))> {};
    struct ReadB : rule<create<Val>(assign<&Val::text>(ref<B>))> {};
  };

  struct Hop : rule<ref<Mutual<5>::B>> {};

  struct Root
      : rule<
            create<Doc>(
                (lit<"mutual/a"> + assign<&Doc::value>(ref<Mutual<1>::A>)) |
                (lit<"mutual/b"> + assign<&Doc::value>(ref<Mutual<2>::B>)) |
                (lit<"mutual/ab:a"> + assign<&Doc::value>(ref<Mutual<3>::A>)) |
                (lit<"mutual/ab:b"> + assign<&Doc::value>(ref<Mutual<3>::B>)) |
                (lit<"mutual/ba:b"> + assign<&Doc::value>(ref<Mutual<4>::B>)) |
                (lit<"mutual/ba:a"> + assign<&Doc::value>(ref<Mutual<4>::A>)) |
                (lit<"mutual-a/a"> + assign<&Doc::value>(ref<MutualA<1>::A>)) |
                (lit<"mutual-a/b"> + assign<&Doc::value>(ref<MutualA<2>::B>)) |
                (lit<"mutual-b/a"> + assign<&Doc::value>(ref<MutualB<1>::A>)) |
                (lit<"mutual-b/b"> + assign<&Doc::value>(ref<MutualB<2>::B>)) |
                (lit<"mutual-ab/a"> +
                 assign<&Doc::value>(ref<MutualAB<1>::A>)) |
                (lit<"mutual-ab/b"> +
                 assign<&Doc::value>(ref<MutualAB<2>::B>)) |
                (lit<"ring/a"> + assign<&Doc::value>(ref<Ring<1>::A>)) |
                (lit<"ring/b"> + assign<&Doc::value>(ref<Ring<2>::B>)) |
                (lit<"ring/c"> + assign<&Doc::value>(ref<Ring<3>::C>)) |
                (lit<"ring-b/a"> + assign<&Doc::value>(ref<RingB<1>::A>)) |
                (lit<"ring-b/b"> + assign<&Doc::value>(ref<RingB<2>::B>)) |
                (lit<"ring-b/c"> + assign<&Doc::value>(ref<RingB<3>::C>)) |
                (lit<"rungs/a"> + assign<&Doc::value>(ref<Rungs<1>::A>)) |
                (lit<"rungs-b/a"> + assign<&Doc::value>(ref<RungsB<1>::A>)) |
                (lit<"share/a"> + assign<&Doc::value>(ref<Share<1>::A>)) |
                (lit<"diamond/a"> + assign<&Doc::value>(ref<Diamond<1>::A>)) |
                (lit<"diamond-s/a"> +
                 assign<&Doc::value>(ref<DiamondS<1>::A>)) |
                (lit<"hop:b"> + assign<&Doc::value>(ref<Hop>)) |
                (lit<"hop:a"> + assign<&Doc::value>(ref<Mutual<5>::A>)) |
                (lit<"paren/a"> + assign<&Doc::value>(ref<Paren<1>::A>)) |
                (lit<"paren/b"> + assign<&Doc::value>(ref<Paren<2>::B>)) |
                (lit<"sub/a"> + assign<&Doc::value>(ref<Sub<1>::A>)) |
                (lit<"sub/b"> + assign<&Doc::value>(ref<Sub<2>::B>)) |
                (lit<"sub-amb/a"> + assign<&Doc::value>(ref<SubAmb<1>::A>)) |
                (lit<"sub-amb/b"> + assign<&Doc::value>(ref<SubAmb<2>::B>)) |
                (lit<"valued/a"> + assign<&Doc::value>(ref<Valued<1>::ReadA>)) |
                (lit<"valued/b"> + assign<&Doc::value>(ref<Valued<2>::ReadB>)) |
                (lit<"valued/ab:a"> +
                 assign<&Doc::value>(ref<Valued<3>::ReadA>)) |
                (lit<"valued/ab:b"> +
                 assign<&Doc::value>(ref<Valued<3>::ReadB>))) +
            eof> {};
  using entry = Root;
};
struct CycleLr : Cycle {};

struct Shared : Common {
  template <int>
  struct Via {
    struct W;
    struct A : rule<ref<W> | create<Bax>(lit<"a"> + lit<"x">)> {};
    struct W : rule<create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">) |
                    create<Ba>(lit<"a">)> {};
  };
  template <int>
  struct ViaA {
    struct W;
    struct A : rule<create<Bax>(lit<"a"> + lit<"x">) | ref<W>> {};
    struct W : rule<create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">) |
                    create<Ba>(lit<"a">)> {};
  };
  template <int>
  struct ViaW {
    struct W;
    struct A : rule<ref<W> | create<Bax>(lit<"a"> + lit<"x">)> {};
    struct W : rule<create<Ba>(lit<"a">) |
                    create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">)> {};
  };
  template <int>
  struct ViaAW {
    struct W;
    struct A : rule<create<Bax>(lit<"a"> + lit<"x">) | ref<W>> {};
    struct W : rule<create<Ba>(lit<"a">) |
                    create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">)> {};
  };

  template <int>
  struct TurnFirst {
    struct A;
    struct W : rule<create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">)> {};
    struct A : rule<ref<W> | create<Bax>(lit<"a"> + lit<"x">) |
                    create<Ba>(lit<"a">)> {};
  };
  template <int>
  struct TurnLast {
    struct A;
    struct W : rule<create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">)> {};
    struct A : rule<create<Bax>(lit<"a"> + lit<"x">) | create<Ba>(lit<"a">) |
                    ref<W>> {};
  };

  template <int>
  struct Rung {
    struct B;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<ref<A> | create<Ba>(lit<"a">)> {};
  };
  template <int>
  struct RungB {
    struct B;
    struct A : rule<create<Tx>(assign<&Tx::inner>(ref<B>) + lit<"x">) |
                    create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<create<Ba>(lit<"a">) | ref<A>> {};
  };

  template <int>
  struct Chain {
    struct B;
    struct C;
    struct A : rule<ref<B> | create<Bax>(lit<"a"> + lit<"x">)> {};
    struct B : rule<ref<C> | create<Bb>(lit<"b">)> {};
    struct C : rule<create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">) |
                    create<Ba>(lit<"a">)> {};
  };

  template <int>
  struct Ladder {
    struct N;
    struct E : rule<create<Post>(assign<&Post::inner>(ref<N>) + lit<"!">) |
                    create<Ba>(lit<"a">)> {};
    struct N
        : rule<create<Neg>(lit<"-"> + assign<&Neg::inner>(ref<N>)) | ref<E>> {};
  };
  template <int>
  struct LadderB {
    struct N;
    struct E : rule<create<Ba>(lit<"a">) |
                    create<Post>(assign<&Post::inner>(ref<N>) + lit<"!">)> {};
    struct N
        : rule<ref<E> | create<Neg>(lit<"-"> + assign<&Neg::inner>(ref<N>))> {};
  };

  template <int>
  struct FSubBase {
    struct B;
    struct A : rule<ref<B> | create<Ba>(lit<"a">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<B>) + lit<"y">) |
                    create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"z">) |
                    create<Bby>(lit<"b"> + lit<"y">) | create<Bb>(lit<"b">)> {};
  };

  template <int>
  struct FValued {
    struct W;
    struct A : rule<as<wrapA>(ref<W> | (lit<"a"> + lit<"x">))> {};
    struct W : rule<as<wrapB>((ref<A> + lit<"x">) | lit<"a">)> {};
    struct ReadA : rule<create<Val>(assign<&Val::text>(ref<A>))> {};
    struct ReadW : rule<create<Val>(assign<&Val::text>(ref<W>))> {};
  };

  template <int>
  struct FParen {
    struct W;
    struct A
        : rule<ref<W> |
               create<Tp>(lit<"("> + assign<&Tp::inner>(ref<W>) + lit<")">) |
               create<Bax>(lit<"a"> + lit<"x">)> {};
    struct W : rule<create<Tx>(assign<&Tx::inner>(ref<A>) + lit<"x">) |
                    create<Ba>(lit<"a">)> {};
  };

  template <int>
  struct FSub {
    struct B;
    struct A : rule<ref<B> | create<Ba>(lit<"a">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<B>) + lit<"y">) |
                    create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"z">) |
                    create<Bb>(lit<"b">)> {};
  };

  template <int>
  struct TwinBases {
    struct B;
    struct A : rule<ref<B> | create<Bc>(lit<"a">)> {};
    struct B : rule<create<Ty>(assign<&Ty::inner>(ref<A>) + lit<"y">) |
                    create<Ba>(lit<"a">)> {};
  };

  struct Root
      : rule<create<Doc>(
                 (lit<"via/a"> + assign<&Doc::value>(ref<Via<1>::A>)) |
                 (lit<"via/w"> + assign<&Doc::value>(ref<Via<2>::W>)) |
                 (lit<"via/aw:a"> + assign<&Doc::value>(ref<Via<3>::A>)) |
                 (lit<"via/aw:w"> + assign<&Doc::value>(ref<Via<3>::W>)) |
                 (lit<"via/wa:w"> + assign<&Doc::value>(ref<Via<4>::W>)) |
                 (lit<"via/wa:a"> + assign<&Doc::value>(ref<Via<4>::A>)) |
                 (lit<"via-a/a"> + assign<&Doc::value>(ref<ViaA<1>::A>)) |
                 (lit<"via-a/w"> + assign<&Doc::value>(ref<ViaA<2>::W>)) |
                 (lit<"via-w/a"> + assign<&Doc::value>(ref<ViaW<1>::A>)) |
                 (lit<"via-w/w"> + assign<&Doc::value>(ref<ViaW<2>::W>)) |
                 (lit<"via-aw/a"> + assign<&Doc::value>(ref<ViaAW<1>::A>)) |
                 (lit<"via-aw/w"> + assign<&Doc::value>(ref<ViaAW<2>::W>)) |
                 (lit<"turn-first/a"> +
                  assign<&Doc::value>(ref<TurnFirst<1>::A>)) |
                 (lit<"turn-first/w"> +
                  assign<&Doc::value>(ref<TurnFirst<2>::W>)) |
                 (lit<"turn-last/a"> +
                  assign<&Doc::value>(ref<TurnLast<1>::A>)) |
                 (lit<"turn-last/w"> +
                  assign<&Doc::value>(ref<TurnLast<2>::W>)) |
                 (lit<"rung/a"> + assign<&Doc::value>(ref<Rung<1>::A>)) |
                 (lit<"rung/b"> + assign<&Doc::value>(ref<Rung<2>::B>)) |
                 (lit<"rung/ab:a"> + assign<&Doc::value>(ref<Rung<3>::A>)) |
                 (lit<"rung/ab:b"> + assign<&Doc::value>(ref<Rung<3>::B>)) |
                 (lit<"rung/ba:b"> + assign<&Doc::value>(ref<Rung<4>::B>)) |
                 (lit<"rung/ba:a"> + assign<&Doc::value>(ref<Rung<4>::A>)) |
                 (lit<"rung-b/a"> + assign<&Doc::value>(ref<RungB<1>::A>)) |
                 (lit<"rung-b/b"> + assign<&Doc::value>(ref<RungB<2>::B>)) |
                 (lit<"chain/a"> + assign<&Doc::value>(ref<Chain<1>::A>)) |
                 (lit<"chain/b"> + assign<&Doc::value>(ref<Chain<2>::B>)) |
                 (lit<"chain/c"> + assign<&Doc::value>(ref<Chain<3>::C>)) |
                 (lit<"ladder/e"> + assign<&Doc::value>(ref<Ladder<1>::E>)) |
                 (lit<"ladder/n"> + assign<&Doc::value>(ref<Ladder<2>::N>)) |
                 (lit<"ladder/en:e"> + assign<&Doc::value>(ref<Ladder<3>::E>)) |
                 (lit<"ladder/en:n"> + assign<&Doc::value>(ref<Ladder<3>::N>)) |
                 (lit<"ladder/ne:n"> + assign<&Doc::value>(ref<Ladder<4>::N>)) |
                 (lit<"ladder/ne:e"> + assign<&Doc::value>(ref<Ladder<4>::E>)) |
                 (lit<"ladder-b/e"> + assign<&Doc::value>(ref<LadderB<1>::E>)) |
                 (lit<"ladder-b/n"> + assign<&Doc::value>(ref<LadderB<2>::N>)) |
                 (lit<"fvalued/a"> +
                  assign<&Doc::value>(ref<FValued<1>::ReadA>)) |
                 (lit<"fvalued/w"> +
                  assign<&Doc::value>(ref<FValued<2>::ReadW>)) |
                 (lit<"fvalued/aw:a"> +
                  assign<&Doc::value>(ref<FValued<3>::ReadA>)) |
                 (lit<"fvalued/aw:w"> +
                  assign<&Doc::value>(ref<FValued<3>::ReadW>)) |
                 (lit<"fparen/a"> + assign<&Doc::value>(ref<FParen<1>::A>)) |
                 (lit<"fparen/w"> + assign<&Doc::value>(ref<FParen<2>::W>)) |
                 (lit<"fsub/a"> + assign<&Doc::value>(ref<FSub<1>::A>)) |
                 (lit<"fsub/b"> + assign<&Doc::value>(ref<FSub<2>::B>)) |
                 (lit<"fsub/ab:a"> + assign<&Doc::value>(ref<FSub<3>::A>)) |
                 (lit<"fsub/ab:b"> + assign<&Doc::value>(ref<FSub<3>::B>)) |
                 (lit<"fsub-base/a"> +
                  assign<&Doc::value>(ref<FSubBase<1>::A>)) |
                 (lit<"fsub-base/b"> +
                  assign<&Doc::value>(ref<FSubBase<2>::B>)) |
                 (lit<"twin-bases/a"> +
                  assign<&Doc::value>(ref<TwinBases<1>::A>))) +
             eof> {};
  using entry = Root;
};
// Every turn of the sub-cycle reads `y`: the election tells the turns apart by
// the order written.
struct Twin : Common {
  struct B;
  struct A : rule<ref<B> | create<Ba>(lit<"a">)> {};
  struct B : rule<create<Ty>(assign<&Ty::inner>(ref<B>) + lit<"y">) |
                  create<Tz>(assign<&Tz::inner>(ref<A>) + lit<"y">) |
                  create<Bb>(lit<"b">)> {};
  struct Root
      : rule<create<Doc>((lit<"twin/a"> + assign<&Doc::value>(ref<A>)) |
                         (lit<"twin/b"> + assign<&Doc::value>(ref<B>))) +
             eof> {};
  using entry = Root;
};
struct Combined : Common {
  struct Root : rule<ref<Shared::Root> | ref<Twin::Root>> {};
  using entry = Root;
};
struct SharedLl : Combined {};
struct SharedLr : Combined {};

}  // namespace tindirectloop
