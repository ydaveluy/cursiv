// A name refusing words another terminal reads with a frontier of its own,
// ranked below the name: what stands where the word refuses the name turns on
// the byte behind the word, which the rival's own frontier answers.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "engines/ll/ReservedBoundAst.hpp"

namespace tllbound {
// The boolean's frontier is the word boundary, which holds a byte the name
// never reads.
struct Ascii : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  static constexpr auto Chars = cr<"a-zA-Z_"> + many(cr<"a-zA-Z0-9_">);
  struct NAME : terminal<!(kw<"true"> | kw<"false">) + Chars> {};
  struct BOOL : terminal<as<bool>(kw<"true"> | kw<"false">)> {};
  struct Name
      : rule<create<Item>(assign<&Item::name>(ref<NAME>) + lit<";">)> {};
  struct Lit
      : rule<create<Flag>(assign<&Flag::value>(ref<BOOL>) + lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Name>) |
                                      append<&Doc::flags>(ref<Lit>)))> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_$">;
};

// Letters past ASCII the name decodes, every scalar past ASCII the boundary
// holds.
struct Wide : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  static constexpr auto Chars =
      (uc::L | cr<"_">) + many(uc::L | uc::Nd | cr<"_">);
  struct NAME : terminal<!(kw<"true"> | kw<"false">) + Chars> {};
  struct BOOL : terminal<as<bool>(kw<"true"> | kw<"false">)> {};
  struct Name
      : rule<create<Item>(assign<&Item::name>(ref<NAME>) + lit<";">)> {};
  struct Lit
      : rule<create<Flag>(assign<&Flag::value>(ref<BOOL>) + lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Name>) |
                                      append<&Doc::flags>(ref<Lit>)))> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto word_boundary = cr<R"(a-zA-Z0-9_\u{80}-\u{10FFFF})">;
};

// Rivals whose frontiers refuse the very bytes behind a refused word: inside
// the words, the letter one of them goes on with; past a word, a byte its
// boundary leaves out.
struct Frontier : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<!(kw<"do"> | kw<"di">) + cr<"a-z"> + many(w)> {};
  struct ONE : terminal<cr<"d"> + !cr<"o">> {};
  struct TWO : terminal<cr<"d"> + cr<"o"> + !cr<"!">> {};
  struct Name
      : rule<create<Item>(assign<&Item::name>(ref<NAME>) + lit<";">)> {};
  struct Marked : rule<create<Mark>((assign<&Mark::one>(ref<ONE>) |
                                     assign<&Mark::two>(ref<TWO>)) +
                                    lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Name>) |
                                      append<&Doc::marks>(ref<Marked>)))> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_$">;
};

// Two rivals inside the word, apart only before a byte that does not go on
// with it: there the word is never spelled.
struct Aside : Frontier {
  struct NAME : terminal<!kw<"do"> + cr<"a-z"> + many(w)> {};
  struct ONE : terminal<cr<"d"> + !cr<"!">> {};
  struct TWO : terminal<cr<"d-e">> {};
  struct Name
      : rule<create<Item>(assign<&Item::name>(ref<NAME>) + lit<";">)> {};
  struct Marked : rule<create<Mark>((assign<&Mark::one>(ref<ONE>) |
                                     assign<&Mark::two>(ref<TWO>)) +
                                    lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Name>) |
                                      append<&Doc::marks>(ref<Marked>)))> {};
  using entry = Root;
  static constexpr auto word_boundary = w;
};

struct AsciiLr : Ascii {};
struct WideLl : Wide {};
struct WideLr : Wide {};
struct FrontierLr : Frontier {};
struct AsideLr : Aside {};
}  // namespace tllbound
