// The grammar of the @CURSIV_NEW_ID@ language, written in C++. The build
// generates the parser from this header.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "@CURSIV_NEW_ID@/core/ast.hpp"

// The sample language declares people and greets them:
//
//     person Alice
//     Hello Alice!
//
// The members of a grammar:
//   * `terminal<...>`  a token, such as a name or a number; `as<T>(...)`
//                      converts its text, for example to a number
//   * `rule<...>`      a rule; it builds the AST class whose members it fills
//   * `entry`          the rule that reads a whole document
//   * `ignored`        tokens allowed anywhere and dropped, such as spaces
//   * `hidden`         tokens allowed anywhere and kept in the syntax tree,
//                      such as comments, which hover can then show
//
// In a rule, `ref<R>` reads the rule or terminal `R`, and these build the AST:
//   * `create<T>`         build a T, even if no member is set
//   * `assign<&T::m>(e)`  read `e` and store it in `m`
//   * `append<&T::m>(e)`  read `e` and add it to the list `m`
//   * `flag<&T::m>(e)`    read `e` and set the bool `m` to true
//   * `nest<&T::m>(e)`    wrap the node built so far in a new T, stored in `m`
//
// If the build refuses the grammar, look up the message on the "Grammar
// errors" page of the Cursiv documentation.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@ : cursiv::GrammarSpec {
  // `s` matches a whitespace character, `w` a word character, and `cr<"...">`
  // a character from a range, written as in a regular expression.
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // `kw` matches a whole keyword, not the start of a longer word; `ikw` does
  // the same in any letter case, and `lit` matches exact text such as `!`.
  struct Person : rule<kw<"person"> + assign<&ast::Person::name>(ref<ID>)> {};

  // `ast::Greeting::person` is a `reference<Person>`: this reads a name, and
  // after parsing the name resolves to the `Person` declared with it. By
  // default, a named node is visible inside the node that contains it.
  struct Greeting
      : rule<kw<"Hello"> + assign<&ast::Greeting::person>(ref<ID>) + lit<"!">> {
  };

  struct Model : rule<create<ast::Model>
                      + some(append<&ast::Model::persons>(ref<Person>)
                             | append<&ast::Model::greetings>(ref<Greeting>))> {
  };

  using entry = Model;
  using ignored = WS;
  using hidden = list<SL_COMMENT, ML_COMMENT>;
};

}  // namespace @CURSIV_NEW_ID@
