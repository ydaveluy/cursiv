// `Type -> Annos Type "[]"` where `Annos` may be empty: the recursion hides
// behind an item that matches nothing.
// expect LR: refused .hiddenLeftRecursion..*rule .Type. reaches itself before matching anything, behind an item that may match nothing
// expect LL: refused .leftRecursion..*rule .Type. reaches itself before matching anything
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Type : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<string_view> marks;
  vector<Type*> body;
};
}  // namespace ast

struct G : Base {
  struct Annos
      : rule<create<ast::Type>(many(append<&ast::Type::marks>(lit<"@">)))> {};
  struct Type
      : rule<create<ast::Type>(append<&ast::Type::body>(ref<Annos>) +
                               append<&ast::Type::body>(ref<Type>) +
                               lit<"[]">) |
             create<ast::Type>(assign<&ast::Type::name>(ref<ID>))> {};
  using entry = Type;
  using ignored = WS;
};

}  // namespace refuse
