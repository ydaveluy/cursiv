#pragma once

// Classes of code points in a token: identifier properties, a general
// category, a complement, and the word boundary of a keyword beside them.
// Character decoding and category tests live in CharactersGrammar.hpp. These
// remaining cases exercise rival scalar guards and intersected reservations.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tunicode {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  bool cond = false;
  bool where = false;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

// A guard of code points: `1.` is a number only where no identifier starts
// and no second dot follows, and `#` only where no identifier starts. Both
// end a match, so the scalar behind it is decoded and may not be one of them.
namespace dot {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<(uc::XID_Start | cr<"_">) + many(uc::XID_Continue)> {};
  struct NUM
      : terminal<some(cr<"0-9">) +
                 option(lit<".", !(cr<"."> | uc::XID_Start)> |
                        (lit<"."> + some(cr<"0-9">)))> {};
  struct DOT : terminal<lit<".">> {};
  struct DOTS : terminal<lit<"..">> {};
  struct HASH : terminal<(lit<"#"> + !uc::XID_Start) | lit<"#_">> {};
  struct Item : rule<create<ast::Item> +
                     assign<&ast::Item::name>(ref<ID> | ref<NUM> | ref<DOT> |
                                              ref<DOTS> | ref<HASH>)> {};
  struct Doc : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
  using entry = Doc;
  using ignored = WS;
};
}  // namespace dot

// One spelling, two terminals refusing two classes of scalars behind it:
// where the first is refused, the second stands as its own class says.
namespace cased {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LOW : terminal<lit<"a", !uc::Lu>> {};
  struct UP : terminal<lit<"a", !uc::Ll>> {};
  struct BIG : terminal<some(uc::Lu)> {};
  struct ACUTE : terminal<lit<"é">> {};
  struct Item : rule<create<ast::Item> +
                     (flag<&ast::Item::cond>(ref<LOW>) |
                      flag<&ast::Item::where>(ref<UP>) |
                      assign<&ast::Item::name>(ref<BIG> | ref<ACUTE>))> {};
  struct Doc : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
  using entry = Doc;
  using ignored = WS;
};
}  // namespace cased

// A keyword's boundary with more of the token behind it: the class is
// refused word by word, so `if¬` is one token and `ifé` none.
namespace inside {
struct G : cursiv::GrammarSpec {
  struct T : terminal<kw<"if"> + lit<"¬">> {};
  struct Item : rule<create<ast::Item> + assign<&ast::Item::name>(ref<T>)> {};
  struct Doc : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
};
}  // namespace inside
// A keyword's boundary joined by a class of bytes: the union keeps the
// scalars, so `if` stands before `¬` and before nothing, not before `!`.
namespace trailing {
struct G : cursiv::GrammarSpec {
  struct T : terminal<kw<"if"> + !cr<"!">> {};
  struct SYM : terminal<some(~(uc::L | uc::Zs | uc::Cc))> {};
  struct Item : rule<create<ast::Item> +
                     assign<&ast::Item::name>(ref<T> | ref<SYM>)> {};
  struct Doc : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
};
}  // namespace trailing
// Two reservations of one word, a boundary of code points and a guard of one
// scalar: where both refuse it the name comes back, so `ifé` is the name `if`
// before `é`. A grammar whole: a lane lowers it alone.
namespace intersect {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Item : rule<create<ast::Item> +
                     !(kw<"if"> |
                       lit<"if", !cr<"é">>)+assign<&ast::Item::name>(ref<ID>) +
                     lit<"é">> {};
  struct Doc : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
  using entry = Doc;
  using ignored = WS;
  static constexpr auto word_boundary = uc::XID_Continue | cr<"$">;
};
}  // namespace intersect

// A spelling guarded against every scalar past ASCII, beside a keyword of the
// same word: where the guard refuses, the keyword stands or falls by the
// scalar behind it.
namespace rawlead {
struct G : cursiv::GrammarSpec {
  struct P : terminal<lit<"if", !cr<R"(\u{80}-\u{10FFFF})">>> {};
  struct K : terminal<kw<"if">> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct SYM : terminal<some(~(uc::L | uc::Zs | uc::Cc))> {};
  struct Item : rule<create<ast::Item> +
                     (assign<&ast::Item::name>(ref<P> | ref<ID> | ref<SYM>) |
                      flag<&ast::Item::cond>(ref<K>))> {};
  struct Doc : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
};
}  // namespace rawlead

// `\x41-\x43` is `A-C`, spelled with an escape or not.
namespace escape {
struct G : cursiv::GrammarSpec {
  struct B : terminal<lit<"b:"> + some(cr<R"(\x41-\x43)">)> {};
  struct U : terminal<lit<"u:"> + some(cr<R"(\x41-\x43é)">)> {};
  struct Item : rule<create<ast::Item> +
                     assign<&ast::Item::name>(ref<B> | ref<U>)> {};
  struct Doc : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
};
}  // namespace escape

// These scalar guard cases share the LR character table.
struct Cases : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Entry : rule<(kw<"inside"> + ref<inside::G::Doc>) |
                      (kw<"intersect"> + ref<intersect::G::Doc>) |
                      (kw<"dot"> + ref<dot::G::Doc>) |
                      (kw<"cased"> + ref<cased::G::Doc>) |
                      (kw<"trailing"> + ref<trailing::G::Doc>) |
                      (kw<"rawlead"> + ref<rawlead::G::Doc>) |
                      (kw<"escape"> + ref<escape::G::Doc>)> {};
};

}  // namespace tunicode
