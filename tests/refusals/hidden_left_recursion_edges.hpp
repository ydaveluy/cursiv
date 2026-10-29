// The same eight optional clauses, each the whole of a `skip()` region: a
// region's edge is no reason to read the first as anything else.
// expect LR: refused .hiddenLeftRecursion..*rule .Type. reaches itself before matching anything, behind more than 7 optional items
// expect LL: refused .leftRecursion..*rule .Type. reaches itself before matching anything
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
                 tight(option(kw<"a"> + assign<&ast::Type::a>(ref<ID>))) +
                 tight(option(kw<"b"> + assign<&ast::Type::b>(ref<ID>))) +
                 tight(option(kw<"c"> + assign<&ast::Type::c>(ref<ID>))) +
                 tight(option(kw<"d"> + assign<&ast::Type::d>(ref<ID>))) +
                 tight(option(kw<"e"> + assign<&ast::Type::e>(ref<ID>))) +
                 tight(option(kw<"f"> + assign<&ast::Type::f>(ref<ID>))) +
                 tight(option(kw<"g"> + assign<&ast::Type::g>(ref<ID>))) +
                 tight(option(kw<"h"> + assign<&ast::Type::h>(ref<ID>))) +
                 append<&ast::Type::body>(ref<Type>) + lit<"[]">) |
             create<ast::Type>(assign<&ast::Type::name>(ref<ID>))> {};
  using entry = Type;
  using ignored = WS;
};

}  // namespace refuse
