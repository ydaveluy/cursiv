#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlldangling {
struct Stmt : cursiv::Extends<cursiv::AstNode> {
  string_view cond;
  Stmt* then_{};
  Stmt* else_{};
};
struct Doc : cursiv::Extends<cursiv::AstNode> { Stmt* stmt{}; };

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct ID : terminal<some(cr<"a-z0-9">)> {};
  struct ElseFirst
      : rule<create<Stmt>(kw<"if"> + assign<&Stmt::cond>(ref<ID>) +
                          kw<"then"> + assign<&Stmt::then_>(ref<ElseFirst>) +
                          kw<"else"> + assign<&Stmt::else_>(ref<ElseFirst>)) |
             create<Stmt>(kw<"if"> + assign<&Stmt::cond>(ref<ID>) +
                          kw<"then"> + assign<&Stmt::then_>(ref<ElseFirst>)) |
             create<Stmt>(assign<&Stmt::cond>(ref<ID>))> {};
  struct ElseLast
      : rule<create<Stmt>(kw<"if"> + assign<&Stmt::cond>(ref<ID>) +
                          kw<"then"> + assign<&Stmt::then_>(ref<ElseLast>)) |
             create<Stmt>(kw<"if"> + assign<&Stmt::cond>(ref<ID>) +
                          kw<"then"> + assign<&Stmt::then_>(ref<ElseLast>) +
                          kw<"else"> + assign<&Stmt::else_>(ref<ElseLast>)) |
             create<Stmt>(assign<&Stmt::cond>(ref<ID>))> {};
  struct ElseOption
      : rule<create<Stmt>(kw<"if"> + assign<&Stmt::cond>(ref<ID>) +
                          kw<"then"> + assign<&Stmt::then_>(ref<ElseOption>) +
                          option(kw<"else"> +
                                 assign<&Stmt::else_>(ref<ElseOption>))) |
             create<Stmt>(assign<&Stmt::cond>(ref<ID>))> {};
  struct ElseCommit
      : rule<create<Stmt>(kw<"if"> + assign<&Stmt::cond>(ref<ID>) + kw<"then"> +
                          assign<&Stmt::then_>(ref<ElseCommit>) +
                          option(commit(kw<"else">,
                                        assign<&Stmt::else_>(ref<ElseCommit>)))) |
             create<Stmt>(assign<&Stmt::cond>(ref<ID>))> {};
  struct Root
      : rule<create<Doc>(
                 (kw<"first"> + assign<&Doc::stmt>(ref<ElseFirst>)) |
                 (kw<"last"> + assign<&Doc::stmt>(ref<ElseLast>)) |
                 (kw<"option"> + assign<&Doc::stmt>(ref<ElseOption>)) |
                 (kw<"commit"> + assign<&Doc::stmt>(ref<ElseCommit>))) +
             eof> {};
  using entry = Root;
  using ignore = WS;
};
struct Lr : Base {};
}  // namespace tlldangling
