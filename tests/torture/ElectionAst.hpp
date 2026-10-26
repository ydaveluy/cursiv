#pragma once

// What the election shapes build: an unrelated pair the construction ranks, a
// rule whose own alternatives are ranked at the election, and a run grouped
// into pairs or triples.

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace telect::ast {

struct XN : cursiv::Extends<cursiv::AstNode> {};
struct YN : cursiv::Extends<cursiv::AstNode> {};
struct Top : cursiv::Extends<cursiv::AstNode> {
  XN* x = nullptr;
  YN* y = nullptr;
};

struct RN : cursiv::Extends<cursiv::AstNode> {};
struct RX : cursiv::Extends<RN> {};
struct RY : cursiv::Extends<RN> {};
struct Hold : cursiv::Extends<cursiv::AstNode> {
  RN* r = nullptr;
};

struct Group : cursiv::Extends<cursiv::AstNode> {};
struct Pair : cursiv::Extends<Group> {};
struct Triple : cursiv::Extends<Group> {};
struct List : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<Group*> groups;
};

}  // namespace telect::ast
