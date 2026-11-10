#pragma once

// After an operand, `<` is either `LT`, which refuses a second `<`, or `BARE`:
// `BARE` goes on with `!`, `LT` with an operand, and a `<` typed right after
// `LT` turns it into `BARE`.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tcaretclimb {

struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view id;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left{};
  string_view op;
  Expr* right{};
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Expr* value{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct BARE : terminal<lit<"<">> {};
  struct LT : terminal<lit<"<", !lit<"<">>> {};
  struct Primary : rule<create<Name>(assign<&Name::id>(ref<ID>)) |
                        (ref<BARE> + ref<Primary> + lit<">">)> {};
  struct Sum : infix<Primary, Bin, left(ref<LT>)> {};
  struct Root : rule<create<Doc>(assign<&Doc::value>(ref<Sum>) +
                                 (lit<";"> | (ref<BARE> + lit<"!">)))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tcaretclimb
