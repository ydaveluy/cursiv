#pragma once

// Two nodes on one CST record: `Pair` opens an `Outer` and, inline on the same
// identifier, its `Inner` child. Both are named `same`, and a multi reference
// that admits both kinds names them with one word: `same;same`.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tsr {

struct Inner : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Outer : cursiv::Extends<cursiv::AstNode> {
  string_view name = "same";
  Inner* child = nullptr;
};
struct Use : cursiv::Extends<cursiv::AstNode> {
  multi_reference<Outer, Inner> target;
};
struct Model : cursiv::Extends<cursiv::AstNode> {
  Outer* pair = nullptr;
  Use* use = nullptr;
};

struct SharedRecord : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Pair : rule<create<Outer>(assign<&Outer::child>(
                    create<Inner>(assign<&Inner::name>(ref<ID>))))> {};
  struct UseRule : rule<assign<&Use::target>(ref<ID>)> {};
  struct Entry : rule<assign<&Model::pair>(ref<Pair>) + lit<";"> +
                      assign<&Model::use>(ref<UseRule>)> {};
  using entry = Entry;
};

}  // namespace tsr
