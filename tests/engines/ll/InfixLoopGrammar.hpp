#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tinfixloop {

struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Ba : cursiv::Extends<Node> {};
struct Bb : cursiv::Extends<Node> {};
struct Bin : cursiv::Extends<Node> {
  Node* left{};
  string_view op;
  Node* right{};
};
struct Neg : cursiv::Extends<Node> {
  Node* inner{};
};
struct Post : cursiv::Extends<Node> {
  Node* inner{};
};
struct Qst : cursiv::Extends<Node> {
  Node* inner{};
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Node* value{};
};

struct Common : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
};

// A postfix and a group inside the primary of a one-level infix.
struct Sum : Common {
  struct E;
  struct P : rule<create<Ba>(lit<"a">) | create<Bb>(lit<"b">) |
                  (lit<"("> + ref<E> + lit<")">) |
                  create<Post>(assign<&Post::inner>(ref<E>) + lit<"!">)> {};
  struct E : infix<P, Bin, left(lit<"+">)> {};
  struct Root : rule<create<Doc>((lit<"e/"> + assign<&Doc::value>(ref<E>)) |
                                 (lit<"p/"> + assign<&Doc::value>(ref<P>))) +
                     eof> {};
  using entry = Root;
  using ignored = WS;
};

// Two left levels, a prefix and a postfix.
struct Ladder : Common {
  struct E;
  struct P : rule<create<Ba>(lit<"a">) | create<Bb>(lit<"b">) |
                  (lit<"("> + ref<E> + lit<")">) |
                  create<Neg>(lit<"-"> + assign<&Neg::inner>(ref<E>)) |
                  create<Post>(assign<&Post::inner>(ref<E>) + lit<"!">)> {};
  struct E : infix<P, Bin, levels(left(lit<"*">), left(lit<"+">))> {};
  struct Root : rule<create<Doc>((lit<"e/"> + assign<&Doc::value>(ref<E>)) |
                                 (lit<"p/"> + assign<&Doc::value>(ref<P>))) +
                     eof> {};
  using entry = Root;
  using ignored = WS;
};

// The wrapper is reached through a rule of its own.
struct Via : Common {
  struct E;
  struct W : rule<create<Qst>(assign<&Qst::inner>(ref<E>) + lit<"?">)> {};
  struct P : rule<create<Ba>(lit<"a">) | create<Bb>(lit<"b">) |
                  (lit<"("> + ref<E> + lit<")">) | ref<W>> {};
  struct E : infix<P, Bin, levels(left(lit<"*">), left(lit<"+">))> {};
  struct Root : rule<create<Doc>((lit<"e/"> + assign<&Doc::value>(ref<E>)) |
                                 (lit<"p/"> + assign<&Doc::value>(ref<P>)) |
                                 (lit<"w/"> + assign<&Doc::value>(ref<W>))) +
                     eof> {};
  using entry = Root;
  using ignored = WS;
};

// A right level below a left one.
struct Mixed : Common {
  struct E;
  struct P : rule<create<Ba>(lit<"a">) | create<Bb>(lit<"b">) |
                  (lit<"("> + ref<E> + lit<")">) |
                  create<Neg>(lit<"-"> + assign<&Neg::inner>(ref<E>)) |
                  create<Post>(assign<&Post::inner>(ref<E>) + lit<"!">)> {};
  struct E : infix<P, Bin, levels(right(lit<"^">), left(lit<"+">))> {};
  struct Root : rule<create<Doc>((lit<"e/"> + assign<&Doc::value>(ref<E>)) |
                                 (lit<"p/"> + assign<&Doc::value>(ref<P>))) +
                     eof> {};
  using entry = Root;
  using ignored = WS;
};
// These tables use the same trivia and operator levels. Select their
// entry rules in one generated language instead of generating each separately.
struct Combined : Common {
  struct Root : rule<(lit<"sum:"> + ref<Sum::Root>) |
                     (lit<"ladder:"> + ref<Ladder::Root>) |
                     (lit<"via:"> + ref<Via::Root>) |
                     (lit<"mixed:"> + ref<Mixed::Root>)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tinfixloop
