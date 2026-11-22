#pragma once

// Delimiters with no indent or dedent: the statement's newline is a token, but
// a blank between an opener and its closer. The word before a newline and the
// word before a closer share one state.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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
  using ignored = list<WS, NL>;
  static constexpr auto indent_open = lit<"("> | lit<"["> | lit<"{">;
  static constexpr auto indent_close = lit<")"> | lit<"]"> | lit<"}">;
  using indent_newline = NEWLINE;
};

}  // namespace tbracketcaret
