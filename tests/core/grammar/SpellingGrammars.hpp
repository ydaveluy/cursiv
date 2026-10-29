#pragma once

// The grammars the spelling lane parses: one `kw<>` under three declared word
// boundaries, an infix chain whose operators go through the same resolution,
// and one keyword in both cases against a class not closed under case.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tspell {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Default : cursiv::GrammarSpec {
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct Doc : rule<kw<"["> + ref<NAME>> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
};
// An override that does not hold '[': the guard stays absent.
struct Digits : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"0-9">;
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct Doc : rule<kw<"["> + ref<NAME>> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
};
// An override that does: the same `kw<"[">` gains it.
struct Bracketed : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"a-z[">;
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct Doc : rule<kw<"["> + ref<NAME>> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
};

struct Plain : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z0-9_-">)> {};
  struct Doc : rule<kw<"if"> + many(ref<NAME>)> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
  using ignored = WS;
};
// CSS-flavoured: `-` is a word character, so `if-x` is one word and `if` may
// not match its head.
struct Dashed : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-zA-Z0-9_-">)> {};
  struct Doc : rule<kw<"if"> + many(ref<NAME>)> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
  using ignored = WS;
};

struct Num : ast::Node {
  std::string_view text;
};
struct Bin : ast::Node {
  ast::Node* left = nullptr;
  std::string_view op;
  ast::Node* right = nullptr;
};
struct Calc : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct INT : terminal<some(d)> {};
  struct Prim : rule<assign<&Num::text>(ref<INT>)> {};
  struct E : infix<Prim, Bin, levels(left(kw<"*">), left(kw<"+">))> {};
  using entry = E;
  using ignored = WS;
};

// The asymmetric class the divergence needs, spelled once per case.
struct ILower : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"a-z">;
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct Doc : rule<ikw<"if"> + many(ref<NAME>)> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
  using ignored = WS;
};
struct IUpper : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"a-z">;
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct Doc : rule<ikw<"IF"> + many(ref<NAME>)> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
  using ignored = WS;
};
// ...and the mirror class, which the naive "fold to lower" answer gets wrong:
// `A-Z` holds `F` and not `f`, so the lowercase spelling needs the guard too.
struct IUpperClass : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"A-Z">;
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"A-Z">)> {};
  struct Doc : rule<ikw<"if"> + many(ref<NAME>)> {};
  struct Root : rule<create<ast::Node>(ref<Doc>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tspell
