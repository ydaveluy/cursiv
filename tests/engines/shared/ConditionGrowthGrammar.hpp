#pragma once

// Conditions tested at each token of a long run, on both engines: runs with
// no maximum read round by round, wherever the rest of their condition stands,
// and conditions the token in hand decides.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcondgrowth {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};

struct Main : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct DASH : terminal<lit<"-">> {};
  // The dashes that end in `>` are one arrow; any other dash is an item.
  struct Arrow : rule<some(ref<DASH>) + lit<">">> {};
  // The token in hand decides both conditions.
  struct Mark : rule<lit<"!"> + !lit<">"> + &ref<DASH> + ref<DASH>> {};
  // A run with no minimum, the rest of its condition after it.
  struct Middle
      : rule<lit<"m"> + ((!(ref<DASH> + many(lit<"=">) + lit<")">)+ref<DASH>) |
                         (ref<DASH> + many(lit<"=">) + lit<")">))> {};
  // A run in the arm of a choice that is the whole condition.
  struct Choice : rule<lit<"c"> + ((!((ref<DASH> + some(lit<"=">) + lit<";">) |
                                      (ref<DASH> + lit<"(">)) +
                                    ref<DASH>) |
                                   (ref<DASH> + some(lit<"=">) + lit<";">) |
                                   (ref<DASH> + lit<"(">))> {};
  // Two runs in a row.
  struct Twice
      : rule<lit<"t"> +
             ((&(some(lit<"=">) + some(lit<"+">) + lit<";">)+lit<"=">) |
              (some(lit<"=">) + lit<"?">))> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(
                    (!(some(ref<DASH>) + lit<">">)+ref<DASH>) | ref<Arrow> |
                    ref<Mark> | ref<Middle> | ref<Choice> | ref<Twice> |
                    lit<";"> | lit<"+"> | lit<"=">)))> {};
  using entry = Root;
  using ignored = WS;
};
struct Lr : Main {};
struct Ll : Main {};

}  // namespace tcondgrowth
