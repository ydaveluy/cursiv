// The workspace fixture's AST. `reference<Type>` accepts a DataType or an
// Entity, so one field serves both.
#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace ws::ast {

struct AbstractElement : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Type : cursiv::Extends<AbstractElement> {};

struct DataType : cursiv::Extends<Type> {};

// `many` records whether the keyword matched, not its text.
struct Feature : cursiv::Extends<cursiv::AstNode> {
  boolean many;
  string_view name;
  reference<Type> type;
};

struct Entity : cursiv::Extends<Type> {
  reference<Entity> superType;
  vector<Feature*> features;
};

struct PackageDeclaration : cursiv::Extends<AbstractElement> {
  vector<AbstractElement*> elements;
};

struct Workspace : cursiv::Extends<cursiv::AstNode> {
  vector<AbstractElement*> elements;
};

}  // namespace ws::ast
