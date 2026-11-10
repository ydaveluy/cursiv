#pragma once

// Two places where a repair inserts in the order the grammar is read, though
// the ids say otherwise: `Root` reaches `y` and `}` first, `Pick` writes `x`
// before `y`, and `Block` reads `m` before `}`.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tinsertorder {

struct Item : cursiv::AstNode {};
struct Doc : cursiv::AstNode {
  Item* item = nullptr;
  Item* block = nullptr;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct Pick : rule<create<Item>(kw<"x">) | create<Item>(kw<"y">)> {};
  struct Block : rule<create<Item>(kw<"{"> + many(kw<"m">) + kw<"}">)> {};
  struct Root : rule<create<Doc>(kw<"q"> + kw<"y"> + kw<"}">) |
                     create<Doc>(assign<&Doc::item>(ref<Pick>) +
                                 assign<&Doc::block>(ref<Block>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tinsertorder
