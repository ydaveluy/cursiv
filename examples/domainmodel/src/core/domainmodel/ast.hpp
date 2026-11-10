// The domainmodel example language: its AST. `type=[Type]` is a
// `reference<Type>`, which accepts a DataType or an Entity.
#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

namespace dm::ast {

struct AbstractElement : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Type : cursiv::Extends<AbstractElement> {};

struct DataType : cursiv::Extends<Type> {};

// `many` is the fact of a match, not matched text -- the grammar's `?=`
// (`flag`) sets the flag and stores nothing.
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

struct DomainModel : cursiv::Extends<cursiv::AstNode> {
  vector<AbstractElement*> elements;
};

}  // namespace dm::ast
