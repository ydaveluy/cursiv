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
struct RequiredLr : Required {};
struct CloneLr : Clone {};
}  // namespace tllunit
