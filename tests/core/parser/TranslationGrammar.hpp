#pragma once

// Declarations and line comments: small enough that a translated document
// and its decoded text can be compared record by record.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace ttrans {

struct Decl : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Unit : cursiv::Extends<cursiv::AstNode> {
  vector<Decl*> decls;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<R"(a-zA-Z_à-ÿ)">)> {};
  struct LINE : terminal<lit<"//"> + many(cr<R"(^\n)">)> {};
  struct DeclR : rule<create<Decl>(kw<"class"> + assign<&Decl::name>(ref<ID>) +
                                   lit<";">)> {};
  struct Root
      : rule<create<Unit>(many(append<&Unit::decls>(ref<DeclR>)))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = list<LINE>;
};

struct Lr : Base {};
struct Ll : Base {};

}  // namespace ttrans
