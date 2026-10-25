#pragma once
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
namespace tnl {
namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
}  // namespace ast
struct NlG : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct EOL : terminal<lit<"\n">> {};
  struct Root
      : rule<create<ast::Doc>(assign<&ast::Doc::name>(ref<ID>) + ref<EOL>)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace tnl
