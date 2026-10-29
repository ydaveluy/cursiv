#pragma once

// Grammars a document embeds whole with `section<>`, both engines reading it.
// Each keeps its trivia, its word boundary, its keywords and its end of input
// in its section, whatever token closes it; the document skips blanks, keeps
// `#` comments and reads a newline as a token, and restates one of Json's
// terminals for itself.

#include <charconv>
#include <concepts>
#include <cstdint>
#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tembed {

namespace ast {
struct Value : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Value> {
  int64 n = 0;
};
struct Str : cursiv::Extends<Value> {
  string_view text;
};
struct Flag : cursiv::Extends<Value> {
  boolean on = false;
};
struct Pair : cursiv::Extends<cursiv::AstNode> {
  string_view key;
  Value* value = nullptr;
};
struct Obj : cursiv::Extends<Value> {
  vector<Pair*> pairs;
};
struct List : cursiv::Extends<Value> {
  vector<Value*> items;
};

struct Set : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  int64 value = 0;
};
struct Lines : cursiv::Extends<cursiv::AstNode> {
  vector<Set*> sets;
};

struct Block : cursiv::Extends<cursiv::AstNode> {
  Value* json = nullptr;
  Lines* script = nullptr;
  int64 count = 0;
  string_view opener;
  vector<Value*> list;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Block*> blocks;
};
}  // namespace ast

// Blanks and newlines dropped, `//` comments kept, keywords in any case, and
// a word boundary a digit does not stand in: `x10` is `x` then `10`.
struct Json : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\r\n)">)> {};
  struct COMMENT : terminal<lit<"//"> + many(cr<R"(^\n)">)> {};
  struct STRING : terminal<lit<"\""> + many(cr<R"(^"\n)">) + lit<"\"">> {};
  struct NUMBER : terminal<as<std::int64_t>(some(d))> {};
  struct Value;
  struct Pair
      : rule<create<ast::Pair>(assign<&ast::Pair::key>(ref<STRING>) + lit<":"> +
                               assign<&ast::Pair::value>(ref<Value>))> {};
  struct Obj
      : rule<create<ast::Obj>(
            lit<"{"> + many(append<&ast::Obj::pairs>(ref<Pair>), lit<",">) +
            lit<"}">)> {};
  struct List
      : rule<create<ast::List>(
            lit<"["> + many(append<&ast::List::items>(ref<Value>), lit<",">) +
            lit<"]">)> {};
  struct Flag : rule<create<ast::Flag>(flag<&ast::Flag::on>(ikw<"true">) |
                                       ikw<"false">)> {};
  struct Num
      : rule<create<ast::Num>(assign<&ast::Num::n>(ref<NUMBER>)) |
             create<ast::Num>(kw<"x"> + assign<&ast::Num::n>(ref<NUMBER>))> {};
  struct Str : rule<create<ast::Str>(assign<&ast::Str::text>(ref<STRING>))> {};
  struct Value : rule<ref<Obj> | ref<List> | ref<Flag> | ref<Num> | ref<Str>> {
  };
  struct Text : rule<ref<Value> + eof> {};
  using entry = Text;
  using ignored = WS;
  using hidden = list<COMMENT>;
  static constexpr auto word_boundary = cr<"a-zA-Z_">;
};

// Lines of assignments, a `;` implied at a line break and at the end of the
// text -- of its section, embedded.
struct Script : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NL : terminal<some(cr<R"(\r\n)">)> {};
  struct ID : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct INT : terminal<as<std::int64_t>(some(d))> {};
  struct Line
      : rule<create<ast::Set>(assign<&ast::Set::name>(ref<ID>) + lit<"="> +
                              assign<&ast::Set::value>(ref<INT>) +
                              implied(lit<";">, gap::line | eof))> {};
  struct Root : rule<create<ast::Lines>(
                    many(append<&ast::Lines::sets>(ref<Line>)) + eof)> {};
  using entry = Root;
  using ignored = list<WS, NL>;
};

// Names under a word boundary of its own, and the keywords it reserves: a
// refused word, a predicate, a keyword the state shares with a name, a keyword
// over trivia spelled the same.
struct Names : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" \t\r\n">)> {};
  struct REM : terminal<kw<"rem"> + some(cr<" \t">) + many(cr<R"(^\n)">)> {};
  struct ID : terminal<cr<"a-z"> + many(cr<"a-z0-9-">)> {};
  static constexpr auto Reserved = kw<"let"> | kw<"var">;
  struct NAME : terminal<!Reserved + cr<"a-z"> + many(cr<"a-z0-9-">)> {};
  struct Let
      : rule<create<ast::Set>(kw<"let"> + assign<&ast::Set::name>(ref<NAME>) +
                              lit<";">)> {};
  struct Get
      : rule<create<ast::Set>(kw<"get"> + assign<&ast::Set::name>(ref<ID>) +
                              lit<";">)> {};
  struct Note
      : rule<create<ast::Set>(kw<"rem"> + assign<&ast::Set::name>(ref<ID>) +
                              lit<";">)> {};
  struct Use
      : rule<create<ast::Set>(!Reserved + assign<&ast::Set::name>(ref<ID>) +
                              lit<";">)> {};
  struct Root : rule<create<ast::Lines>(
                    many(append<&ast::Lines::sets>(ref<Let> | ref<Get> |
                                                   ref<Note> | ref<Use>)) +
                    eof)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = list<REM>;
};
// A word class the host's `w` is wider than, and one it is narrower than.
struct Narrow : Names {
  static constexpr auto word_boundary = cr<"a-z">;
};
struct Dash : Names {
  static constexpr auto word_boundary = cr<"a-z0-9_-">;
};
struct Wide : Names {
  static constexpr auto word_boundary = uc::L | uc::Nd | cr<"_">;
};

// Words of letters past ASCII, a keyword's spelling refused them where the
// class ends it.
struct Letters : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" \t\r\n">)> {};
  static constexpr auto Reserved = kw<"let"> | kw<"var">;
  struct WORD : terminal<!Reserved + some(uc::L | uc::Nd | cr<"_">)> {};
  struct Item
      : rule<create<ast::Set>(assign<&ast::Set::name>(ref<WORD>) + lit<";">)> {
  };
  struct Root : rule<create<ast::Lines>(
                    many(append<&ast::Lines::sets>(ref<Item>)) + eof)> {};
  using entry = Root;
  using ignored = WS;
  static constexpr auto word_boundary = uc::L | uc::Nd | cr<"_">;
};

// Nothing skipped: a state of its section and one of the document's tight
// region may want the same tokens.
struct Tiny : cursiv::GrammarSpec {
  struct Root : rule<create<ast::Flag>(flag<&ast::Flag::on>(lit<"a">) +
                                       lit<";">)> {};
  using entry = Root;
};

inline constexpr auto from_hex = [](std::string_view t) {
  std::int64_t v = 0;
  std::from_chars(t.data() + 2, t.data() + t.size(), v, 16);
  return v;
};

struct Doc : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NL : terminal<some(lit<"\n">)> {};
  struct CM : terminal<lit<"#"> + many(cr<R"(^\n)">)> {};

  // Json's number, read by the document as hexadecimal: the section still
  // reads Json's own.
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Json::NUMBER>
  struct override<T> : terminal<as<from_hex>(lit<"0x"> + some(cr<"0-9a-f">))> {
  };

  struct Data : rule<create<ast::Block>(kw<"json"> +
                                        assign<&ast::Block::json>(section<Json>(
                                            lit<"'''">, lit<"'''">)))> {};
  // The document's blanks cut between the delimiters around a second section
  // of Json, closed by another token; the section's own edges stay Json's.
  struct Raw : rule<create<ast::Block>(
                   kw<"raw"> + tight(lit<"("> +
                                     assign<&ast::Block::json>(
                                         section<Json>(lit<"<">, lit<">">)) +
                                     lit<")">))> {};
  struct Each
      : rule<create<ast::Block>(
            kw<"each"> +
            append<&ast::Block::list>(section<Json>(
                assign<&ast::Block::opener>(lit<"(">), lit<")">)) +
            append<&ast::Block::list>(section<Json>(lit<"(">, lit<")">)))> {};
  struct Count
      : rule<create<ast::Block>(
            kw<"count"> + assign<&ast::Block::count>(ref<Json::NUMBER>))> {};
  struct Small
      : rule<create<ast::Block>(
            kw<"tiny"> +
            tight(assign<&ast::Block::json>(section<Tiny>(lit<"{">, lit<"}">)) +
                  lit<";">))> {};
  struct Narrowed
      : rule<create<ast::Block>(
            kw<"narrow"> +
            assign<&ast::Block::script>(section<Narrow>(lit<"<">, lit<">">)))> {
  };
  struct Dashed : rule<create<ast::Block>(
                      kw<"dash"> + assign<&ast::Block::script>(
                                       section<Dash>(lit<"<">, lit<">">)))> {};
  struct Widened : rule<create<ast::Block>(
                       kw<"wide"> + assign<&ast::Block::script>(
                                        section<Wide>(lit<"<">, lit<">">)))> {};
  struct Lettered
      : rule<create<ast::Block>(kw<"letters"> +
                                assign<&ast::Block::script>(
                                    section<Letters>(lit<"<">, lit<">">)))> {};
};

struct DocLl : Doc {
  struct Line : rule<(ref<Data> | ref<Raw> | ref<Each> | ref<Count> |
                      ref<Small> | ref<Narrowed> | ref<Dashed> | ref<Widened> |
                      ref<Lettered>)+ref<NL>> {};
  struct Root
      : rule<create<ast::Doc>(many(append<&ast::Doc::blocks>(ref<Line>)))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = list<CM>;
};

struct DocLr : Doc {
  struct Code : rule<create<ast::Block>(
                    kw<"script"> + assign<&ast::Block::script>(section<Script>(
                                       lit<"<%">, lit<"%>">)))> {};
  struct Line : rule<(ref<Data> | ref<Raw> | ref<Each> | ref<Count> |
                      ref<Small> | ref<Narrowed> | ref<Dashed> | ref<Widened> |
                      ref<Lettered> | ref<Code>)+ref<NL>> {};
  struct Root
      : rule<create<ast::Doc>(many(append<&ast::Doc::blocks>(ref<Line>)))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = list<CM>;
};

}  // namespace tembed
