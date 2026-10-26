#pragma once
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
namespace tllunit {
struct Document : cursiv::Extends<cursiv::AstNode> {
  string_view padding;
};
struct Required : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\r\n)">)> {};
  struct Padding : rule<some(cr<R"( \t\r\n)">)> {};
  struct CountedPadding : rule<repeat<2, 4>(cr<R"( \t\r\n)">)> {};
  struct Root
      : rule<((lit<"x"> + assign<&Document::padding>(ref<Padding>) + lit<"y">) |
              (lit<"["> + assign<&Document::padding>(ref<CountedPadding>) +
               gap::none + lit<"]">)) + eof> {};
  using entry = Root;
  using ignore = WS;
};
// Identical whole-run readings are not protected units. Their skip region
// still decides where they are trivia and where they are visible data.
struct Clone : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct VISIBLE_WS : terminal<some(cr<R"( \t)">)> {};
  struct Tight
      : rule<tight(lit<"["> + assign<&Document::padding>(ref<VISIBLE_WS>) +
                       lit<"]">)> {};
  struct Root : rule<ref<Tight> + lit<"x"> + lit<"y"> + eof> {};
  using entry = Root;
  using ignore = WS;
};

// A blank is both trivia and a visible token of the same length.
struct Name : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Stmts : cursiv::Extends<cursiv::AstNode> {
  vector<Name*> stmts;
};
struct Blank : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct GAP : terminal<some(s)> {};
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct GapRule : rule<assign<&Name::text>(ref<GAP>)> {};
  struct WordRule : rule<assign<&Name::text>(ref<WORD>)> {};
  struct Root : rule<some(append<&Stmts::stmts>(ref<GapRule> | ref<WordRule>)) + eof> {};
  using entry = Root;
  using ignore = WS;
};

// Visible blanks, counted character units and an identical trivia terminal
// share one parser. Each entry retains its own whitespace configuration.
struct Ll : cursiv::GrammarSpec {
  struct Root
      : rule<(lit<"required:"> +
              skip(ignored<Required::WS>, hide<>, ref<Required::Root>)) |
             (lit<"clone:"> +
              skip(ignored<Clone::WS>, hide<>, ref<Clone::Root>)) |
             (lit<"blank:"> +
              skip(ignored<Blank::WS>, hide<>,
                   ref<Blank::Root>))> {};
  using entry = Root;
};
}  // namespace tllunit
