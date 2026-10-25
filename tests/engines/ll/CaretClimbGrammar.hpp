#pragma once

// An operator guarded against a second `<` and its bare twin, both read after
// an operand: the `!` goes on from the twin, the operand from the operator,
// and a `<` typed right behind the operator makes it the twin.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

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
  struct Sum : infix<Primary, &Bin::left, &Bin::op, &Bin::right,
                     left(ref<LT>)> {};
  struct Root : rule<create<Doc>(assign<&Doc::value>(ref<Sum>) +
                                 (lit<";"> | (ref<BARE> + lit<"!">)))> {};
  using entry = Root;
  using ignore = WS;
};

struct Ll : Base {};
struct Lr : Base {};

}  // namespace tcaretclimb
