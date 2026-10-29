// `Type -> K1 ... K8 Type "[]"`: eight optional clauses that each consume, in
// front of the recursive call -- the gate reads the last as an item that may
// match nothing.
// expect LR: refused .hiddenLeftRecursion..*rule .Type. reaches itself before matching anything, behind more than 7 optional items
// expect LL: refused .leftRecursion..*rule .Type. reaches itself before matching anything inside a repetition or behind optional items.*at most 7 optional items
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Type : cursiv::Extends<cursiv::AstNode> {
  string_view name, a, b, c, d, e, f, g, h;
  vector<Type*> body;
};
}  // namespace ast

struct G : Base {
  struct Type
      : rule<create<ast::Type>(
                 option(kw<"a"> + assign<&ast::Type::a>(ref<ID>)) +
                 option(kw<"b"> + assign<&ast::Type::b>(ref<ID>)) +
                 option(kw<"c"> + assign<&ast::Type::c>(ref<ID>)) +
                 option(kw<"d"> + assign<&ast::Type::d>(ref<ID>)) +
                 option(kw<"e"> + assign<&ast::Type::e>(ref<ID>)) +
                 option(kw<"f"> + assign<&ast::Type::f>(ref<ID>)) +
                 option(kw<"g"> + assign<&ast::Type::g>(ref<ID>)) +
                 option(kw<"h"> + assign<&ast::Type::h>(ref<ID>)) +
                 append<&ast::Type::body>(ref<Type>) + lit<"[]">) |
             create<ast::Type>(assign<&ast::Type::name>(ref<ID>))> {};
  using entry = Type;
  using ignored = WS;
};

}  // namespace refuse
