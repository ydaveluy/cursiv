#pragma once

// A grammar that declares delimiters without laying an indent or a dedent: the
// statement's newline is a token, and between an opener and its closer a
// blank. The word before a newline and the word before a closer are one state,
// which reads both.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tbracketcaret {

struct Word : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Word*> words;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NEWLINE : terminal<eol> {};
  struct NL : terminal<eol> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct WordRule : rule<create<Word>(assign<&Word::name>(ref<ID>))> {};
  struct Root
      : rule<create<Doc>(
            many(append<&Doc::words>(ref<WordRule>) +
                 option((lit<"("> + many(append<&Doc::words>(ref<WordRule>)) +
                         lit<")">) |
                        (lit<"["> + many(append<&Doc::words>(ref<WordRule>)) +
                         lit<"]">) |
                        (lit<"{"> + many(append<&Doc::words>(ref<WordRule>)) +
                         lit<"}">)) +
                 ref<NEWLINE>))> {};
  using entry = Root;
  using ignore = list<WS, NL>;
  static constexpr auto indent_open = lit<"("> | lit<"["> | lit<"{">;
  static constexpr auto indent_close = lit<")"> | lit<"]"> | lit<"}">;
  using indent_newline = NEWLINE;
};

}  // namespace tbracketcaret
