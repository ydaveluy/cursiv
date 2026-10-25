#pragma once

// A class template an author instantiates with strings, one holding `<` and
// `,` and one a number no signed literal holds: the two engines write each
// type's name into their units as the author spelled it.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace detail {
struct CursivTag {};
}

namespace ttag {

namespace ast {
template <cursiv::str S>
struct Tag : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
template <auto Value, class Marker>
struct Numeric : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Tag<"a<b,c">* first = nullptr;
  Tag<"99999999999999999999">* second = nullptr;
  vector<cursiv::AstNode*> numbers;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-z"> + many(w)> {};
  struct First : rule<create<ast::Tag<"a<b,c">>(
                     assign<&ast::Tag<"a<b,c">::name>(ref<ID>))> {};
  struct Second
      : rule<create<ast::Tag<"99999999999999999999">>(
            assign<&ast::Tag<"99999999999999999999">::name>(ref<ID>))> {};
  template <auto Value, cursiv::str Word>
  struct Number : rule<create<ast::Numeric<Value, ::detail::CursivTag>>(
      assign<&ast::Numeric<Value, ::detail::CursivTag>::name>(lit<Word>))> {};
  struct Root : rule<create<ast::Holder>(
                    assign<&ast::Holder::first>(ref<First>) + lit<","> +
                    assign<&ast::Holder::second>(ref<Second>) +
                    many(lit<","> + append<&ast::Holder::numbers>(
                        ref<Number<1, "i">> | ref<Number<1L, "l">> |
                        ref<Number<1U, "u">> | ref<Number<1UL, "z">>)))> {};
  using entry = Root;
  using ignore = WS;
};

struct Lr : Base {};
struct Ll : Base {};

}  // namespace ttag
