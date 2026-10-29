#pragma once

// A choice of 101 keywords at three sites exceeds both the template-depth
// budget of 100 and a byte-sized position count. Folding once per element
// still fails, without growing the generated parser beyond those boundaries.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// clang-format off
#define TWIDE_10(X, p)                                                    \
  X(p##0) X(p##1) X(p##2) X(p##3) X(p##4) X(p##5) X(p##6) X(p##7) X(p##8) \
  X(p##9)
#define TWIDE_100(X, p)                                                   \
  TWIDE_10(X, p##0) TWIDE_10(X, p##1) TWIDE_10(X, p##2) TWIDE_10(X, p##3) \
  TWIDE_10(X, p##4) TWIDE_10(X, p##5) TWIDE_10(X, p##6) TWIDE_10(X, p##7) \
  TWIDE_10(X, p##8) TWIDE_10(X, p##9)
// clang-format on
#define TWIDE_OR(w) | ikw<#w>

namespace twidechoice {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};

struct Base : cursiv::GrammarSpec {
  static constexpr auto Words = ikw<"w"> TWIDE_100(TWIDE_OR, a);

  struct WS : terminal<some(s)> {};
  struct ID : terminal<!Words + cr<"a-z"> + many(w)> {};
  struct Root
      : rule<create<Doc>(many((lit<"0"> + append<&Doc::words>(Words)) |
                              (lit<"1"> + append<&Doc::words>(Words)) |
                              (lit<"2"> + append<&Doc::words>(Words)) |
                              (lit<"7"> + append<&Doc::words>(ref<ID>))))> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace twidechoice

#undef TWIDE_OR
#undef TWIDE_100
#undef TWIDE_10
