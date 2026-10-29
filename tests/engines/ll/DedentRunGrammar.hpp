#pragma once

// A loop over `dedent`, which closes a level and consumes no byte: each turn
// closes the next level, as many as the text leaves open.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlldedentrun {
struct Closure : cursiv::Extends<cursiv::AstNode> {};
struct Node : cursiv::Extends<cursiv::AstNode> {
  vector<Closure*> closures;
};
struct Marks : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
  vector<string_view> indents;
  vector<string_view> dedents;
};

struct Common : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct NL : terminal<eol> {};
  struct Closing : rule<create<Closure>(dedent)> {};
};

struct Some : Common {
  struct Root
      : rule<create<Node>(lit<"a"> + ref<NL> + indent + lit<"b"> + ref<NL> +
                          indent + lit<"c"> + ref<NL> +
                          some(append<&Node::closures>(ref<Closing>)) +
                          lit<"d"> + eof)> {};
  using entry = Root;
  using ignored = WS;
};

struct Many : Common {
  struct Root
      : rule<create<Node>(lit<"a"> + ref<NL> + indent + lit<"b"> + ref<NL> +
                          indent + lit<"c"> + ref<NL> +
                          many(append<&Node::closures>(ref<Closing>)) +
                          lit<"d"> + eof)> {};
  using entry = Root;
  using ignored = WS;
};

struct Flat : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct NEWLINE : terminal<eol + many(many(cr<" ">) + eol)> {};
  struct INDENT : terminal<indent> {};
  struct DEDENT : terminal<dedent> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<Marks>(many(
                    append<&Marks::names>(ref<ID>) | lit<":"> | ref<NEWLINE> |
                    append<&Marks::indents>(ref<INDENT>) |
                    append<&Marks::dedents>(ref<DEDENT>)))> {};
  using entry = Root;
  using ignored = WS;
  using indent_newline = NEWLINE;
};
struct FlatLl : Flat {};
// `tail:` ends its entry two levels deep, before the text does.
struct Combined : Common {
  struct Root : rule<(lit<"some:"> + ref<Some::Root>) |
                     (lit<"many:"> + ref<Many::Root>) |
                     create<Node>(lit<"tail:"> + lit<"a"> + ref<NL> + indent +
                                  lit<"b"> + ref<NL> + indent + lit<"c">)> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Combined {};
}  // namespace tlldedentrun
