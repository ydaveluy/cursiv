#pragma once

// Each body entered by its first token: a tight region, where a stray space is
// the one byte a repair can only delete, and a right-recursive sum, where after
// a primary the state may shift another `+` or reduce -- the ambiguity an
// unwind resolves by deleting exactly the operator the resumed state owes.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tmsg {

struct Head : cursiv::Extends<cursiv::AstNode> {};
struct Sum : cursiv::Extends<cursiv::AstNode> {};

struct MessageG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};
  static constexpr auto head =
      tight(lit<"["> + ref<ID> + lit<"]">);
  struct Tight : rule<create<Head>(head)> {};
  struct ExprRule;
  struct Primary : rule<ref<NUM> | (lit<"("> + ref<ExprRule> + lit<")">)> {};
  struct ExprRule
      : rule<(ref<Primary> + lit<"+"> + ref<ExprRule>) | ref<Primary>> {};
  struct Choice : rule<create<Sum>(ref<ExprRule> + lit<";">)> {};
  struct Root : rule<ref<Tight> | ref<Choice>> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tmsg
