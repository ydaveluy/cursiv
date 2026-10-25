// The requirements example language: its AST. Two entry rules share one
// rule set -- Contact, ID and STRING are written once and reached from both
// `RequirementModel` and `TestModel`.
#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace req::ast {

struct Contact : cursiv::Extends<cursiv::AstNode> {
  string_view userName;
};

struct Environment : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view description;
};

struct Requirement : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view text;
  vector<reference<Environment>> environments;
};

struct RequirementModel : cursiv::Extends<cursiv::AstNode> {
  Contact* contact;
  vector<Environment*> environments;
  vector<Requirement*> requirements;
};

// `requirements` and `environments` cross into whichever `.req` the workspace
// holds: names declared there are exported to a global index, so a reference
// here resolves across documents, not just within this one.
struct Test : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view testFile;  // empty when the optional clause is absent
  vector<reference<Requirement>> requirements;
  vector<reference<Environment>> environments;
};

struct TestModel : cursiv::Extends<cursiv::AstNode> {
  Contact* contact;
  vector<Test*> tests;
};

}  // namespace req::ast
