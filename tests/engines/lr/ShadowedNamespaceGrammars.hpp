#pragma once

// A grammar declared where bare `cursiv::` and `std::` name the author's own
// namespaces: its generated code spells both from the global scope. Beside it,
// a precedence table in a region whose two engines' readings give one AST.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tshadow::cursiv::std {

namespace ast {
struct Doc : ::cursiv::Extends<::cursiv::AstNode> {
  vector<string_view> items;
};
struct Bin : ::cursiv::Extends<::cursiv::AstNode> {
  Bin* left{};
  string_view op;
  Bin* right{};
};
struct Wrap : ::cursiv::Extends<::cursiv::AstNode> {
  Bin* value{};
};
}  // namespace ast

struct Base : ::cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};
  struct Doc
      : rule<create<ast::Doc>(many(append<&ast::Doc::items>(ref<ID>)))> {};
  struct Primary : rule<create<ast::Bin>(assign<&ast::Bin::op>(ref<NUM>))> {};
  struct Sum : infix<Primary, ast::Bin, (left(lit<"+">))> {};
  struct Region
      : rule<create<ast::Wrap>(
            tight(lit<"["> + assign<&ast::Wrap::value>(ref<Sum>) + lit<"]">))> {
  };
  struct Root : rule<ref<Doc> | ref<Region>> {};
  using entry = Root;
  using ignored = WS;
};
struct Lr : Base {};
struct Ll : Base {};

}  // namespace tshadow::cursiv::std
