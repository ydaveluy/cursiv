// The grammar of the @CURSIV_NEW_ID@ language, written in C++. The build
// generates the parser from this header.
#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "@CURSIV_NEW_ID@/ast.hpp"

// The sample language declares people and greets them: `person Alice`, then
// `Hello Alice!`. The Cursiv documentation describes the grammar's parts, and
// its "Grammar errors" page each message the build may refuse it with.

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
