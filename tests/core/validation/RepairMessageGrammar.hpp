#pragma once

// Each body entered by its first token: a tight region, where an extra space
// can only be deleted, and a right-recursive sum, where an unwind deletes
// exactly the operator the resumed state expects.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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
  using ignored = WS;
};

}  // namespace tmsg
