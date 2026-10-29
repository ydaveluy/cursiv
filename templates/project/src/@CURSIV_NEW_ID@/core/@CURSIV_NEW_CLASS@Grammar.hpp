// The @CURSIV_NEW_ID@ language: its grammar. A cursiv grammar is a C++ type --
// there is no grammar file to parse, and no generator reads this header at
// build time except the one compiled against it.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "@CURSIV_NEW_ID@/core/ast.hpp"

// A hello-world language, and every piece of it is a piece you will reuse:
//
//     // people, and greetings for them
//     person Alice
//     person Bob
//
//     Hello Alice!
//
// The four kinds of member:
//   * `terminal<...>`    a token the lexer produces; `as<T>(...)` around
//                         its body converts its text (a number, say)
//   * `rule<...>`        a parser rule, its AST class deduced from the
//                         members it writes -- nothing is annotated
//   * `entry`            the rule a document starts at
//   * `ignored`/`hidden` trivia: `ignored` is dropped, `hidden` stays in the
//                        tree, which is what lets a comment attach to what
//                        follows it
//
// `ref<R>` calls the rule or terminal `R`. The directives build the tree:
//   * `create<T>`         make a T here, even if nothing else writes to it
//   * `assign<&T::m>(e)`  parse `e`, store it in `m` (once)
//   * `append<&T::m>(e)`  parse `e`, push it onto the list `m`
//   * `flag<&T::m>(e)`    parse `e`, then set the bool `m` to true
//   * `nest<&T::m>(e)`    wrap the node built so far in a new T, which
//                         holds it in `m`
//
// One trap: rules that reduce in a cycle without reading text are refused by
// name -- `A -> B` with `B -> A`, or a rule that reaches itself past symbols
// deriving nothing. Make one step of the cycle match at least one token.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@ : cursiv::GrammarSpec {
  // `s` is the whitespace class, `w` the word class, `cr<"...">` a character
  // range written as in a regular expression.
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // `kw` is a keyword (a whole word, never a prefix of a longer one); `ikw` is
  // its case-insensitive twin, and `lit` is raw text with no word boundary.
  struct Person : rule<kw<"person"> + assign<&ast::Person::name>(ref<ID>)> {};

  // `person` is a `reference<Person>` in the AST, so this reads a name and
  // the linker resolves it after the parse: the default scope makes every
  // named node visible inside its container.
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
