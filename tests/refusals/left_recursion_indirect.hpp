// A cycle through a rule that is also called from outside: `Primary` reaches
// `Glob`, whose first position is the whole `Expr` of which `Primary` is the
// operand. Both engines read it.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace cycle {
struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expression> {
  string_view text;
};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct Glob : cursiv::Extends<Expression> {
  Expression* value{};
  bool trailing{};
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Expression*> values;
};
}  // namespace cycle

struct G : Base {
  struct MULT : terminal<lit<"*">> {};
  struct NameRule : rule<assign<&cycle::Name::text>(ref<ID>)> {};
  struct Expr;
  struct GlobRule
      : rule<create<cycle::Glob>(assign<&cycle::Glob::value>(ref<Expr>) +
                                 flag<&cycle::Glob::trailing>(lit<"*">))> {
  };
  struct Primary : rule<ref<NameRule> | ref<GlobRule>> {};
  struct Expr : infix<Primary, cycle::Binary, left(ref<MULT>)> {};
  struct Root
      : rule<create<cycle::Document>(
            append<&cycle::Document::values>(ref<Primary>) + eof)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
