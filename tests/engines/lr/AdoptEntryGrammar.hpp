#pragma once

// An entry that adopts a child's node while reading tokens around it: the
// root is that node, whatever stands beside it.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tadopt {

namespace ast {
struct Clade : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Clade* clade;
};
}  // namespace ast

struct Words : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Label : terminal<some(cr<"A-Za-z">)> {};
  struct Clade : rule<create<ast::Clade> +
                      assign<&ast::Clade::name>(ref<Label>)> {};
};

// The entry adopts, and a token follows the child or an optional one
// precedes it.
namespace token {
struct G : Words {
  struct SHEBANG : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  struct Tree : rule<(ref<Clade> + lit<";">) |
                     (option(ref<SHEBANG>) + ref<Clade>)> {};
  using entry = Tree;
  using ignore = WS;
};
}  // namespace token

// The adopting rule is not the entry: the entry creates and assigns.
namespace inner {
struct G : Words {
  struct Tree : rule<ref<Clade> + lit<";">> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::clade>(ref<Tree>))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace inner

struct G : Words {
  struct Root : rule<ref<token::G::Tree> |
                     (kw<"inner"> + ref<inner::G::Root>)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tadopt
