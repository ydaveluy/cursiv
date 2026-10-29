#pragma once

// The shapes the byte-buffer tests parse: a class that admits `\0`, `any` as a
// width, and one grammar where a class that refuses it stands beside a quoted
// run and the trivia that run up to a closer.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tsent {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct Blob : cursiv::GrammarSpec {
  struct BLOB : terminal<some(cr<"^;">)> {};
  struct Doc : rule<create<ast::Node>(ref<BLOB> + lit<";">)> {};
  using entry = Doc;
};

struct Word : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct LINE : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct STR : terminal<lit<"\""> + many(cr<R"(^")">) + lit<"\"">> {};
  struct Doc
      : rule<create<ast::Node>((ref<NAME> + lit<";">) | ref<STR>)> {};
  using entry = Doc;
  using ignored = list<WS, BLOCK, LINE>;
};

struct Free : cursiv::GrammarSpec {
  struct THREE : terminal<any + any + any> {};
  struct Doc : rule<create<ast::Node>(ref<THREE> + lit<"!">)> {};
  using entry = Doc;
};

}  // namespace tsent
