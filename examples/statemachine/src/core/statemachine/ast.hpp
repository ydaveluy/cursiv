// The statemachine example language: its AST. `vector`, `string_view` and
// `reference` are `cursiv::AstNode`'s aliases, and a `string_view` field lives
// as long as the source text.
#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

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
