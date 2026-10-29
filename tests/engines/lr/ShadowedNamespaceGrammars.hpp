#pragma once

// A grammar declared where a bare `cursiv::` and a bare `std::` both name the
// author's own namespaces: what is generated for it spells both from the
// global scope. Beside its names, a precedence table inside a region, whose
// two readings, the descent's and the table's, must be the one AST.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

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
