// A priority terminal refusing a word at its head stands nowhere there, and
// the position reads again without it: the longest acceptance left wins, one
// inside the run of the refused terminal's own state included.
#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace trefusedshorter {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view wide;
  string_view narrow;
  boolean bang = false;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

// After its lead word each case reads `P`, refused where `abab!` starts, then
// `A`. `head`: `A` is shorter. `wide`: `A` runs over `P`'s bytes and stands
// before any byte but `!`. `run`: `A` stands only before an `a`.
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct HeadP : terminal<!lit<"abab!"> + lit<"abab">> {};
  struct HeadA : terminal<lit<"ab">> {};
  struct WideP : terminal<!lit<"abab!"> + some(cr<"ab">)> {};
  struct WideA : terminal<some(cr<"ab">) + !cr<"!">> {};
  struct RunP : terminal<!lit<"abab!"> + some(cr<"ab">)> {};
  struct RunA : terminal<some(cr<"ab">) + !cr<"b!">> {};

  struct HeadItem : rule<create<Item>(assign<&Item::wide>(ref<HeadP>) |
                                      assign<&Item::narrow>(ref<HeadA>) |
                                      flag<&Item::bang>(lit<"!">))> {};
  struct WideItem : rule<create<Item>(assign<&Item::wide>(ref<WideP>) |
                                      assign<&Item::narrow>(ref<WideA>) |
                                      flag<&Item::bang>(lit<"!">))> {};
  struct RunItem : rule<create<Item>(assign<&Item::wide>(ref<RunP>) |
                                     assign<&Item::narrow>(ref<RunA>) |
                                     flag<&Item::bang>(lit<"!">))> {};
  struct Root : rule<create<Doc>(
                    (lit<"head"> + many(append<&Doc::items>(ref<HeadItem>))) |
                    (lit<"wide"> + many(append<&Doc::items>(ref<WideItem>))) |
                    (lit<"run"> + many(append<&Doc::items>(ref<RunItem>))))> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace trefusedshorter
