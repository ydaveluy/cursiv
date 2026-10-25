#pragma once

// A choice whose alternatives open on a condition and build an empty node:
// the node spelled inline or behind a rule of its own, the choice reads the
// same texts.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tguardedchoice {

struct Mark : cursiv::Extends<cursiv::AstNode> {};
struct Fast : cursiv::Extends<Mark> {};
struct Slow : cursiv::Extends<Mark> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Mark* mark{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
};

struct Inline : Base {
  struct Pick : rule<(gap::line + create<Fast>) | (gap::none + create<Slow>)> {
  };
  struct Root
      : rule<create<Doc>(lit<"("> + assign<&Doc::mark>(ref<Pick>) + lit<")">)> {
  };
  using entry = Root;
  using ignore = WS;
};

struct Named : Base {
  struct FastRule : rule<create<Fast>> {};
  struct SlowRule : rule<create<Slow>> {};
  struct Pick
      : rule<(gap::line + ref<FastRule>) | (gap::none + ref<SlowRule>)> {};
  struct Root
      : rule<create<Doc>(lit<"("> + assign<&Doc::mark>(ref<Pick>) + lit<")">)> {
  };
  using entry = Root;
  using ignore = WS;
};

}  // namespace tguardedchoice
