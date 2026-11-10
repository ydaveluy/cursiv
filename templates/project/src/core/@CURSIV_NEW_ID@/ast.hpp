// The AST classes of the @CURSIV_NEW_ID@ language. The grammar fills their
// members when it parses a document.
#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

// `cursiv::Extends<cursiv::AstNode>` provides the member types below: text in
// `string_view` or `string`, children in `T*` and `vector<T*>`, and a name that
// resolves to a `T` after parsing in `reference<T>`.

namespace @CURSIV_NEW_ID@::ast {

struct Person : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Greeting : cursiv::Extends<cursiv::AstNode> {
  reference<Person> person;
};

struct Model : cursiv::Extends<cursiv::AstNode> {
  vector<Person*> persons;
  vector<Greeting*> greetings;
};

}  // namespace @CURSIV_NEW_ID@::ast
