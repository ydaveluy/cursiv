// The statemachine example language: its AST. Deriving `cursiv::AstNode`
// brings the field vocabulary into scope: `vector`, `string_view`, and
// `reference` below are the base's aliases. Every `string_view` field
// borrows a slice of the source buffer, valid only as long as it is.
#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace sm::ast {

struct Event : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Command : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct State;

struct Transition : cursiv::Extends<cursiv::AstNode> {
  reference<Event> event;
  reference<State> state;
};

struct State : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<reference<Command>> actions;
  vector<Transition*> transitions;
};

struct Statemachine : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Event*> events;
  vector<Command*> commands;
  reference<State> init;
  vector<State*> states;
};

}  // namespace sm::ast
