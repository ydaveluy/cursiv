// The same eight optional clauses in front of the call, and three behind it:
// the clauses behind the call do not take the place of those in front.
// expect LR: refused .hiddenLeftRecursion..*rule .Type. reaches itself before matching anything, behind more than 7 optional items
// expect LL: refused .leftRecursion..*rule .Type. reaches itself before matching anything inside a repetition or behind optional items.*at most 7 optional items
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Type : cursiv::Extends<cursiv::AstNode> {
  string_view name, a, b, c, d, e, f, g, h, i, j, k;
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
                 append<&ast::Type::body>(ref<Type>) +
                 option(kw<"i"> + assign<&ast::Type::i>(ref<ID>)) +
                 option(kw<"j"> + assign<&ast::Type::j>(ref<ID>)) +
                 option(kw<"k"> + assign<&ast::Type::k>(ref<ID>)) +
                 lit<"[]">) |
             create<ast::Type>(assign<&ast::Type::name>(ref<ID>))> {};
  using entry = Type;
  using ignored = WS;
};

}  // namespace refuse
