#pragma once

// Two places where the order a repair inserts in is the order the grammar is
// read in, though the ids say otherwise. The first alternative of `Root`
// meets `y` and `}` before anything: `Pick` writes `x` before `y`, and
// `Block` goes on with `m` before it closes with `}`.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

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
  using ignore = WS;
};

}  // namespace tinsertorder
