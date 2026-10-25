#pragma once

// Nine words documented at 8 KiB each: the prose outweighs the 16-bit pool the
// names of a table live in, and no table carries it.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

#define TDOCS_ROW "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_"
#define TDOCS_X4(x) x x x x
#define TDOCS_KIB TDOCS_X4(TDOCS_X4(TDOCS_ROW))
#define TDOCS_8KIB TDOCS_X4(TDOCS_KIB) TDOCS_X4(TDOCS_KIB)

namespace tdocs {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Word
      : rule<create<ast::Doc>(many(
            append<&ast::Doc::words>(kw<"k0">.doc<TDOCS_8KIB>() |
                                     kw<"k1">.doc<TDOCS_8KIB>() |
                                     kw<"k2">.doc<TDOCS_8KIB>() |
                                     kw<"k3">.doc<TDOCS_8KIB>() |
                                     kw<"k4">.doc<TDOCS_8KIB>() |
                                     kw<"k5">.doc<TDOCS_8KIB>() |
                                     kw<"k6">.doc<TDOCS_8KIB>() |
                                     kw<"k7">.doc<TDOCS_8KIB>() |
                                     kw<"k8">.doc<TDOCS_8KIB>())))> {};
  using entry = Word;
  using ignore = WS;
};
// The same grammar under the descent.
struct Ll : G {};

}  // namespace tdocs

#undef TDOCS_8KIB
#undef TDOCS_KIB
#undef TDOCS_X4
#undef TDOCS_ROW
