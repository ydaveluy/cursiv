#pragma once

// A grammar with a `skip()` region for each of N terminals: each region skips
// its own `#n` between the two words it holds, and nothing else.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

#define SKIPCFG_10(X, H)                                                       \
  X(H##0) X(H##1) X(H##2) X(H##3) X(H##4) X(H##5) X(H##6) X(H##7) X(H##8)      \
      X(H##9)
#define SKIPCFG_ROWS_80(X)                                                     \
  SKIPCFG_10(X, ) SKIPCFG_10(X, 1) SKIPCFG_10(X, 2) SKIPCFG_10(X, 3)           \
      SKIPCFG_10(X, 4) SKIPCFG_10(X, 5) SKIPCFG_10(X, 6) SKIPCFG_10(X, 7)

#define SKIPCFG_TERM(N) struct T##N : terminal<lit<"#" #N>> {};
#define SKIPCFG_REGION(N)                                    \
  +skip(ignore<T##N>, hide<>,                               \
        append<&Doc::words>(lit<"x" #N>) +                   \
            append<&Doc::words>(lit<"y" #N>))

namespace tskipcfg {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};

struct Wide : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  SKIPCFG_ROWS_80(SKIPCFG_TERM)
  struct Root : rule<create<Doc>(lit<"begin"> SKIPCFG_ROWS_80(SKIPCFG_REGION))> {
  };
  using entry = Root;
  using ignored = WS;
};
}  // namespace tskipcfg
