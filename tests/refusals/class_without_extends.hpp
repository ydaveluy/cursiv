// A class that derives from a node class says so with `Extends`: a reference
// to the base resolves through the declared bases, and inheritance alone leaves
// the class out of them.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Person : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Guest : Person {
  string_view nick;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Person*> people;
};
}  // namespace ast

struct G : Base {
  struct PersonRule
      : rule<create<ast::Person>(assign<&ast::Person::name>(ref<ID>))> {};
  struct GuestRule
      : rule<create<ast::Guest>(lit<"guest"> +
                                assign<&ast::Guest::nick>(ref<ID>))> {};
  struct Root
      : rule<create<ast::Doc>(many(append<&ast::Doc::people>(
            ref<PersonRule> | ref<GuestRule>)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
