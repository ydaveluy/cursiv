#pragma once

// The dangling else: `if a then if b then x else e` reads two ways, and no
// precedence says which -- the table keeps both actions and the engine elects.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "torture/DanglingAst.hpp"

namespace tdang {

struct DeepIf : cursiv::GrammarSpec {
  struct Stmt;
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct ID : terminal<some(cr<"a-z0-9">)> {};
  struct Stmt : rule<create<ast::Stmt>(
                         kw<"if"> + assign<&ast::Stmt::cond>(ref<ID>) +
                         kw<"then"> + assign<&ast::Stmt::then_>(ref<Stmt>)) |
                     create<ast::Stmt>(
                         kw<"if"> + assign<&ast::Stmt::cond>(ref<ID>) +
                         kw<"then"> + assign<&ast::Stmt::then_>(ref<Stmt>) +
                         kw<"else"> + assign<&ast::Stmt::else_>(ref<Stmt>)) |
                     create<ast::Stmt>(assign<&ast::Stmt::cond>(ref<ID>))> {};
  using entry = Stmt;
  using ignored = WS;
};

}  // namespace tdang
