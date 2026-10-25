// The arithmetics example language: its AST.
#pragma once

#include <cstdint>

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace arith::ast {

struct Statement : cursiv::Extends<cursiv::AstNode> {};

struct Expression : cursiv::Extends<cursiv::AstNode> {};

// The nameable things a call can point at: `Definition` or
// `DeclaredParameter` -- a base class, not a union, so a reference needs no
// downcast. Sits under `Statement`, though no rule creates one there.
struct AbstractDefinition : cursiv::Extends<Statement> {
  string_view name;
};

struct DeclaredParameter : cursiv::Extends<AbstractDefinition> {};

struct Definition : cursiv::Extends<AbstractDefinition> {
  vector<DeclaredParameter*> args;
  Expression* expr;
};

struct Evaluation : cursiv::Extends<Statement> {
  Expression* expression;
};

// The operation, not the text that denotes it: each operator token hands its
// constant over, so no reader downstream compares spellings.
enum class Operator : std::uint8_t {
  add,
  subtract,
  multiply,
  divide,
  exponent,
  modulo,
};

struct BinaryExpression : cursiv::Extends<Expression> {
  Expression* left;
  Operator op;
  Expression* right;
};

// A value, not the text that denotes it: the rule names a number, so the
// converter runs and the field holds it.
struct NumberLiteral : cursiv::Extends<Expression> {
  float64 value;
};

// Names the definition or parameter it applies -- `func` targets the base
// so both fit -- and a bare identifier is a call with no argument list.
struct FunctionCall : cursiv::Extends<Expression> {
  reference<AbstractDefinition> func;
  vector<Expression*> args;
};

struct GroupedExpression : cursiv::Extends<Expression> {
  Expression* expression;
};

struct Module : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Statement*> statements;
};

}  // namespace arith::ast
