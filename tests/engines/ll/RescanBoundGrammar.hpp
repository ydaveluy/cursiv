#pragma once

// Two terminals that one state of the lexer accepts, each with a frontier of
// its own: the first refuses the byte `;` behind it, the second ends at a word.
// A misspelling is read again as either, judged on its own frontier.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "engines/ll/RescanBoundAst.hpp"

namespace tllrescan {
// The bounded terminal ranks first, and is the state's own election.
struct Narrow : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct BOUNDED : terminal<lit<"abc"> + !cr<";">> {};
  struct LONGER : terminal<lit<"abc"> + many(lit<"d">)> {};
  struct Bounded
      : rule<create<Item>(assign<&Item::text>(ref<BOUNDED>) + lit<".">)> {};
  struct Longer
      : rule<create<Item>(assign<&Item::text>(ref<LONGER>) + lit<";">)> {};
  struct Root : rule<create<Doc>(some(append<&Doc::items>(ref<Bounded>) |
                                      append<&Doc::items>(ref<Longer>)))> {};
  using entry = Root;
  using ignored = WS;
};

// The spelled one is elected where both accept, and is declared last.
struct Shadow : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct BOUNDED : terminal<lit<"abc"> + !cr<";">> {};
  struct SPELLED : terminal<lit<"abc">> {};
  struct Bounded
      : rule<create<Item>(assign<&Item::text>(ref<BOUNDED>) + lit<".">)> {};
  struct Spelled
      : rule<create<Item>(assign<&Item::text>(ref<SPELLED>) + lit<";">)> {};
  struct Root : rule<create<Doc>(some(append<&Doc::items>(ref<Bounded>) |
                                      append<&Doc::items>(ref<Spelled>)))> {};
  using entry = Root;
  using ignored = WS;
};

struct NarrowLl : Narrow {};
struct NarrowLr : Narrow {};
struct ShadowLl : Shadow {};
struct ShadowLr : Shadow {};
}  // namespace tllrescan
