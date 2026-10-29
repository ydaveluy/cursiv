// Readings that part at more than one decision: the first the text meets
// decides, its first written arm winning -- an inner suffix loop before the
// closer of the rule around it, an alternative before the option a later
// position decides, an outer choice before the inner one nearer the conflict;
// a keyword the lexer ranks first decides before any arm.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlltextorder {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Node> {
  string_view text;
};
struct Mark : cursiv::Extends<Node> {
  Node* value{};
};
struct Close : cursiv::Extends<cursiv::AstNode> {};

struct Box : cursiv::Extends<Node> {
  Node* value{};
  Close* close{};
};

struct Title : cursiv::Extends<Node> {};
struct Both : cursiv::Extends<Node> {
  Node* left{};
  Node* right{};
};

struct Compare : cursiv::Extends<Node> {
  Node* left{};
  Node* right{};
};
struct Match : cursiv::Extends<Node> {
  Node* left{};
  Node* right{};
};
struct Add : cursiv::Extends<Node> {
  Node* left{};
  string_view op;
  Node* right{};
};
struct Decl : cursiv::Extends<Node> {
  Node* type{};
  Node* name{};
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> items;
};

struct Common : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NameRule : rule<create<Name>(assign<&Name::text>(ref<ID>))> {};
  struct CloseRule : rule<create<Close>(lit<">">)> {};
};

// A paragraph's loop of lines against the document's loop of blocks, whatever
// block stands before it; the leading option fuses the document's reductions
// that read the loop with those that read the option.

template <bool First>
constexpr auto in_order(auto written, auto other) {
  if constexpr (First)
    return written | other;
  else
    return other | written;
}

// The rungs of a ladder that a left recursion closes into one component: in
// each rung the wrapping arm is written before the bare one or after it.
template <bool CompareFirst, bool TrimFirst = true, bool BoxFirst = true,
          bool MatchFirst = true, bool AliasedAgo = false>
struct Cycle : Common {
  struct WORD : terminal<some(cr<"a-d">)> {};
  struct FROM : terminal<kw<"from">> {};
  struct PLUS : terminal<lit<"+">> {};
  struct AgoSuffix : rule<kw<"ago">> {};
  static constexpr auto ago = [] {
    if constexpr (AliasedAgo)
      return ref<AgoSuffix>;
    else
      return kw<"ago">;
  }();
  struct Atom : rule<create<Name>(assign<&Name::text>(ref<WORD>))> {};
  struct Str;
  struct Core
      : rule<create<Mark>(kw<"min"> + assign<&Mark::value>(ref<Atom>)) |
             create<Both>(kw<"min"> + assign<&Both::left>(ref<Atom>) +
                          kw<"from"> + assign<&Both::right>(ref<Atom>)) |
             ref<Atom>> {};
  struct Foot
      : rule<in_order<MatchFirst>(
            create<Match>(assign<&Match::left>(ref<Str>) + kw<"matches"> +
                          assign<&Match::right>(ref<Str>)),
            ref<Core>)> {};
  struct Ago
      : rule<in_order<BoxFirst>(
            create<Box>(assign<&Box::value>(ref<Foot>) + ago), ref<Foot>)> {};
  struct Before
      : rule<in_order<CompareFirst>(
            create<Compare>(assign<&Compare::left>(ref<Ago>) + ref<FROM> +
                            assign<&Compare::right>(ref<Ago>)),
            ref<Ago>)> {};
  struct Sum : infix<Before, Add, left(ref<PLUS>)> {};
  struct Str : rule<in_order<TrimFirst>(
                   create<Box>(kw<"trim"> + assign<&Box::value>(ref<Str>)),
                   ref<Sum>)> {};
  struct Root : rule<create<Doc>(some(append<&Doc::items>(ref<Str>))) + eof> {};
  using entry = Root;
  using ignored = WS;
};
// The same consuming suffix through a parser rule must preserve the rung's
// association; the other families keep the literal spelling above.


// The arm that reads a child and a tail against the arm that reads the child
// alone, both wrapping it: the arm decides before the loop the child opens.
struct Whole : Common {
  struct Atom : rule<create<Title>(lit<"x">)> {};
  struct Head
      : rule<create<Mark>(many(lit<"x">) + assign<&Mark::value>(ref<Atom>))> {};
  struct Item : rule<create<Both>(assign<&Both::left>(ref<Head>) +
                                  assign<&Both::right>(ref<Atom>)) |
                     create<Box>(assign<&Box::value>(ref<Head>))> {};
  struct Root : rule<create<Doc>(append<&Doc::items>(ref<Item>)) + eof> {};
  using entry = Root;
  using ignored = WS;
};

// A declaration whose type name refuses a reserved word against an expression
// that shifts the same name: the one written first reads it.
template <bool DeclFirst>
struct Refusal : Common {
  struct TypeRule : rule<!kw<"if"> + ref<NameRule>> {};
  struct SpecRule : rule<many(kw<"const">) + ref<TypeRule>> {};
  struct DeclRule
      : rule<create<Decl>(assign<&Decl::type>(ref<SpecRule>) + lit<"*"> +
                          assign<&Decl::name>(ref<NameRule>) + lit<";">)> {};
  struct ProductRule
      : rule<create<Both>(assign<&Both::left>(ref<NameRule>) + lit<"*"> +
                          assign<&Both::right>(ref<NameRule>) + lit<";">)> {};
  struct Item : rule<in_order<DeclFirst>(ref<DeclRule>, ref<ProductRule>)> {};
  struct Root : rule<create<Doc>(some(append<&Doc::items>(ref<Item>))) + eof> {
  };
  using entry = Root;
  using ignored = WS;
};

// A keyword the word terminal also spells, where the condition above the name
// parted before it -- the `!` operand closing a whole condition keeps the
// table's cell there: the lexer ranks the keyword first, before any arm.
struct Keyword : Common {
  struct Implies;
  struct Atom
      : rule<ref<NameRule> |
             create<Box>(lit<"!"> + assign<&Box::value>(ref<Implies>))> {};
  struct AndRule
      : rule<create<Both>(assign<&Both::left>(ref<AndRule>) + kw<"and"> +
                          assign<&Both::right>(ref<Atom>)) |
             ref<Atom>> {};
  struct Matched
      : rule<ref<AndRule> |
             create<Mark>(assign<&Mark::value>(ref<AndRule>) + lit<"?">)> {};
  struct Unmatched
      : rule<create<Both>(assign<&Both::left>(ref<AndRule>) + kw<"implies"> +
                          assign<&Both::right>(ref<Atom>))> {};
  struct Implies : rule<ref<Matched> | ref<Unmatched>> {};
  struct Root
      : rule<create<Doc>(some(append<&Doc::items>(ref<Implies>))) + eof> {};
  using entry = Root;
  using ignored = WS;
};
struct Combined : Common {
  struct Root : rule<(lit<"cycle:"> + ref<Cycle<true, true, true, true, true>::Root>) |
                     (lit<"last:"> + ref<Cycle<false, false, false, false>::Root>) |
                     (lit<"whole:"> + ref<Whole::Root>) |
                     (lit<"first:"> + ref<Refusal<true>::Root>) |
                     (lit<"refusal:"> + ref<Refusal<false>::Root>) |
                     (lit<"keyword:"> + ref<Keyword::Root>)> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Combined {};
}  // namespace tlltextorder
