#pragma once

// A command is one of sixty keywords: the state before it can insert more
// terminals than any bound on the order may cover, and the keyword written
// first is not the one with the least id.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tmenu {

namespace ast {
struct Unit : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LAST : terminal<lit<"z99">> {};

  struct CommandRule
      : rule<(lit<"a00"> | lit<"a01"> | lit<"a02"> | lit<"a03"> | lit<"a04"> |
              lit<"a05"> | lit<"a06"> | lit<"a07"> | lit<"a08"> | lit<"a09"> |
              lit<"a10"> | lit<"a11"> | lit<"a12"> | lit<"a13"> | lit<"a14"> |
              lit<"a15"> | lit<"a16"> | lit<"a17"> | lit<"a18"> | lit<"a19"> |
              lit<"a20"> | lit<"a21"> | lit<"a22"> | lit<"a23"> | lit<"a24"> |
              lit<"a25"> | lit<"a26"> | lit<"a27"> | lit<"a28"> | lit<"a29"> |
              lit<"a30"> | lit<"a31"> | lit<"a32"> | lit<"a33"> | lit<"a34"> |
              lit<"a35"> | lit<"a36"> | lit<"a37"> | lit<"a38"> | lit<"a39"> |
              lit<"a40"> | lit<"a41"> | lit<"a42"> | lit<"a43"> | lit<"a44"> |
              lit<"a45"> | lit<"a46"> | lit<"a47"> | lit<"a48"> | lit<"a49"> |
              lit<"a50"> | lit<"a51"> | lit<"a52"> | lit<"a53"> | lit<"a54"> |
              lit<"a55"> | lit<"a56"> | lit<"a57"> | lit<"a58"> |
              ref<LAST>)+lit<";">> {};

  struct UnitRule : rule<create<ast::Unit>(some(ref<CommandRule>))> {};

  using entry = UnitRule;
  using ignore = WS;
};

struct Lr : Base {};
struct Ll : Base {};

}  // namespace tmenu
