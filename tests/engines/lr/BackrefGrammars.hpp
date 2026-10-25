#pragma once

// Terminals whose closer echoes a run their opener chose, and runs relayed
// inside a repetition. The shapes are cases of one table, behind an opening
// keyword each, but the long bracket: its comment is hidden, and a hidden
// terminal is read in every mode of its table.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tbackref {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
}  // namespace ast

struct Level {};

namespace lua {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  static constexpr auto longBracket =
      through(lit<"["> + mark<Level>(many(cr<"=">)) + lit<"[">,
              lit<"]"> + same<Level> + lit<"]">);
  struct LONG : terminal<longBracket> {};
  // The short form may not start on a `[` that could open a long one, or a
  // same-line `--[[ x ]] y` would run to the end of the line.
  static constexpr auto first = cr<R"(^\r\n[)"> | lit<"[", !cr<"[=">>;
  struct COMMENT
      : terminal<lit<"--"> +
                 option(longBracket | (first + many(cr<R"(^\r\n)">)))> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct LBRACK : terminal<lit<"[">> {};
  struct RBRACK : terminal<lit<"]">> {};
  struct EQ : terminal<lit<"=">> {};
  struct Root
      : rule<create<ast::Doc>(many(
            append<&ast::Doc::items>(ref<LONG> | ref<ID>) |
            (ref<LBRACK> + ref<RBRACK>) | ref<EQ>))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = COMMENT;
};
}  // namespace lua

namespace cpp {
struct G : cursiv::GrammarSpec {
  struct RAW
      : terminal<through(option(lit<"u8"> | lit<"u"> | lit<"U"> | lit<"L">) +
                             lit<"R\""> + mark<Level>(many(cr<"a-zA-Z0-9_">)) +
                             lit<"(">,
                         lit<")"> + same<Level> + lit<"\"">)> {};
  struct STRING : terminal<lit<"\""> + many(cr<R"(^"\n)">) + lit<"\"">> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Root : rule<create<ast::Doc>(
                    many(append<&ast::Doc::items>(ref<RAW> | ref<STRING> |
                                                  ref<ID>)))> {};
};
}  // namespace cpp

namespace twin {
struct G : cursiv::GrammarSpec {
  struct TWIN : terminal<lit<"<"> + mark<Level>(some(cr<"a-z">)) + lit<"-"> +
                         same<Level> + lit<">">> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Root : rule<create<ast::Doc>(
                    many(append<&ast::Doc::items>(ref<TWIN> | ref<ID>)))> {};
};
}  // namespace twin

// The twin echo where the mode reads several -- `a` twice, as the guard
// fixtures spell it -- so the sweep searches the echo too.
namespace multi {
struct G : cursiv::GrammarSpec {
  struct TWIN : terminal<lit<"<"> + mark<Level>(some(cr<"a-z">)) + lit<"-"> +
                         same<Level> + lit<">">> {};
  struct OTHER : terminal<lit<"<"> + mark<Level>(some(cr<"a-z">)) + lit<"-"> +
                          same<Level> + lit<">">> {};
  struct TAIL : terminal<lit<"<"> + mark<Level>(some(cr<"a-z">)) + lit<"-"> +
                         same<Level> + lit<">_">> {};
  static constexpr auto open = lit<"@"> + mark<Level>(many(cr<"a-z">)) + lit<":">;
  struct FIRST : terminal<through(open, same<Level> + lit<"!">)> {};
  struct LAST : terminal<through(open, same<Level> + lit<"?">)> {};
  struct AMBIG : terminal<lit<"~"> + mark<Level>(many(cr<"a">)) + lit<"a"> +
                          same<Level> + lit<"!">> {};
  struct BOUNDED
      : terminal<through(
            lit<"["> + mark<Level>(repeat<65, 128>(cr<"=">)) + lit<"[">,
            lit<"0123456789012345678901234567890123456789"> + same<Level> +
                lit<"]">)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct B : terminal<lit<"a">> {};
  struct A : terminal<lit<"a", !cr<";">>> {};
  struct Root : rule<create<ast::Doc>(
                    many((append<&ast::Doc::items>(ref<TWIN>) + lit<"!">) |
                         (append<&ast::Doc::items>(ref<OTHER>) + lit<"?">) |
                         append<&ast::Doc::items>(ref<TAIL> | ref<FIRST> |
                                                  ref<LAST> | ref<AMBIG> | ref<BOUNDED> | ref<ID>) |
                         (ref<B> + lit<"_">) | (ref<A> + lit<";">)))> {};
};
}  // namespace multi

// A relayed run inside a repetition: a balanced group beside a class that
// holds its opener, beside one that does not, an echo repeated, and an echo
// behind a fence its mark's class holds.
namespace repeat {
struct Echo {};
struct Twice {};
struct G : cursiv::GrammarSpec {
  struct Group
      : terminal<lit<"{"> + many(ref<Group> | (!lit<"{"> + !lit<"}"> + any)) +
                 lit<"}">> {};
  struct Parens
      : terminal<lit<"("> + many(ref<Parens> | (!lit<"("> + !lit<")"> + any)) +
                 lit<")">> {};
  struct WIDE : terminal<lit<"a:"> + some(ref<Group> | cr<"a-z{}">)> {};
  struct NARROW : terminal<lit<"b:"> + some(ref<Group> | cr<"a-z">)> {};
  struct ARG : terminal<lit<"c:"> + some(ref<Parens> | cr<R"(^;\n)">)> {};
  struct TWIN : terminal<lit<"r:"> + some(lit<"<"> + mark<Echo>(some(cr<"a-z">)) +
                                          lit<">"> + same<Echo>)> {};
  struct FENCE : terminal<lit<"f:<"> + mark<Twice>(many(cr<"a">)) + lit<"a"> +
                          same<Twice>> {};
  struct Root
      : rule<create<ast::Doc>(
            many(append<&ast::Doc::items>(ref<WIDE> | ref<NARROW> | ref<ARG> |
                                          ref<TWIN> | ref<FENCE>) +
                 lit<";">))> {};
};
}  // namespace repeat

// A heredoc whose closing tag may be indented: a run of blanks between the
// line break and the echo.
namespace heredoc {
struct G : cursiv::GrammarSpec {
  struct DOC
      : terminal<through(lit<"<<"> + mark<Level>(some(cr<"A-Z">)) + lit<"\n">,
                         lit<"\n"> + many(cr<R"( \t)">) + same<Level>)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Root : rule<create<ast::Doc>(
                    many(append<&ast::Doc::items>(ref<DOC> | ref<ID>)))> {};
};
}  // namespace heredoc

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Entry : rule<(kw<"cpp"> + ref<cpp::G::Root>) |
                      (kw<"heredoc"> + ref<heredoc::G::Root>) |
                      (kw<"twin"> + ref<twin::G::Root>) |
                      (kw<"multi"> + ref<multi::G::Root>) |
                      (kw<"repeat"> + ref<repeat::G::Root>)> {};
  using entry = Entry;
  using ignore = WS;
};

}  // namespace tbackref
