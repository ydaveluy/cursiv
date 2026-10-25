// Two named terminals of one priority over one spelling: a tie the arms
// decide, the first written winning whichever terminal the rules reach first.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlllextie {
struct Value : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Value> { string_view text; };
struct Number : cursiv::Extends<Value> { string_view text; };
struct Doc : cursiv::Extends<cursiv::AstNode> { Value* value{}; };

// The same arms written the other way, `NAT` reached first.

struct Bang : cursiv::Extends<Value> { string_view text; };

// A tie inside a cell: the name's first arm shifts it, its third reduces an
// empty prefix first, and the number's arm stands between them. Where the
// first dies, the number's arm is the first that stands.
struct Cell : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAT : terminal<some(d)> {};
  struct NUM : terminal<some(d) + option(lit<"."> + some(d))> {};
  struct At : rule<option(lit<"@">)> {};
  struct BangRule : rule<create<Bang>(assign<&Bang::text>(ref<NAT>) + lit<";"> +
                                      lit<"!">)> {};
  struct NumberRule
      : rule<create<Number>(assign<&Number::text>(ref<NUM>) + lit<";">)> {};
  struct NameRule : rule<create<Name>(ref<At> + assign<&Name::text>(ref<NAT>) +
                                      lit<";">)> {};
  struct Root : rule<create<Doc>(assign<&Doc::value>(
                         ref<BangRule> | ref<NumberRule> | ref<NameRule>)) +
                     eof> {};
  using entry = Root;
  using ignore = WS;
};

struct Member : cursiv::Extends<cursiv::AstNode> {};
struct Ctor : cursiv::Extends<Member> {
  bool isConst{};
  string_view className;
};
struct Method : cursiv::Extends<Member> {
  string_view returnType;
  string_view name;
};
struct Class : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Member*> members;
};

// A type name and a name over one spelling, the type refusing words at its
// head, the name reached first; behind annotations and an optional keyword
// the member's arms part. `Chest();` is a constructor where its arm is
// written first, a method where the method's is.
struct Members : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_">;
  struct WS : terminal<some(s)> {};
  static constexpr auto IdBody = cr<"a-zA-Z_"> + many(cr<"a-zA-Z0-9_">);
  static constexpr auto Accessor = kw<"get"> | kw<"set">;
  struct TYPE_ID : terminal<!Accessor + IdBody> {};
  struct ID : terminal<IdBody> {};
  struct K_CLASS : terminal<kw<"class">> {};
  struct K_CONST : terminal<kw<"const">> {};
  struct Note : rule<lit<"@"> + ref<ID>> {};
  struct Params : rule<lit<"("> + lit<")">> {};
  struct CtorDecl
      : rule<create<Ctor>(option(flag<&Ctor::isConst>(ref<K_CONST>)) +
                          assign<&Ctor::className>(ref<TYPE_ID>) + ref<Params> +
                          lit<";">)> {};
  struct MethodDecl
      : rule<create<Method>(option(assign<&Method::returnType>(ref<TYPE_ID>)) +
                            assign<&Method::name>(ref<ID>) + ref<Params> +
                            lit<";">)> {};
};

struct CtorFirst : Members {
  struct Entry : rule<many(ref<Note>) + (ref<CtorDecl> | ref<MethodDecl>)> {};
  struct Root : rule<create<Class>(
                    ref<K_CLASS> + assign<&Class::name>(ref<ID>) + lit<"{"> +
                    many(append<&Class::members>(ref<Entry>)) + lit<"}">)> {};
  using entry = Root;
  using ignore = WS;
};

struct MethodFirst : Members {
  struct Entry : rule<many(ref<Note>) + (ref<MethodDecl> | ref<CtorDecl>)> {};
  struct Root : rule<create<Class>(
                    ref<K_CLASS> + assign<&Class::name>(ref<ID>) + lit<"{"> +
                    many(append<&Class::members>(ref<Entry>)) + lit<"}">)> {};
  using entry = Root;
  using ignore = WS;
};

// Two names over one spelling, which a keyword reserves where it is read
// beside them: only the mode without the keyword reads a tie there.
struct Reserved : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A : terminal<cr<"x"> | (cr<"a"> + cr<"a">)> {};
  struct B : terminal<cr<"x"> | (cr<"b"> + cr<"b">)> {};
  struct NameRule : rule<create<Name>(assign<&Name::text>(ref<A>))> {};
  struct NumberRule : rule<create<Number>(assign<&Number::text>(ref<B>))> {};
  struct Plain : rule<create<Bang>(kw<"x"> + lit<";">) |
                      (ref<NameRule> + lit<":">) |
                      (ref<NumberRule> + lit<",">)> {};
  struct Tied : rule<lit<"#"> + ((ref<NameRule> + lit<"!">) |
                                 (ref<NumberRule> + lit<"?">))> {};
  struct Root
      : rule<create<Doc>(assign<&Doc::value>(ref<Plain> | ref<Tied>)) + eof> {
  };
  using entry = Root;
  using ignore = WS;
};
struct ReservedLl : Reserved {};
struct ReservedLr : Reserved {};

struct Fuzzy : cursiv::Extends<cursiv::AstNode> {
  Number* similarity{};
};
struct Fuzzed : cursiv::Extends<Value> {
  Value* primary{};
  vector<Fuzzy*> fuzzy;
};
struct Field : cursiv::Extends<Value> {
  string_view name;
  Value* value{};
};
struct Compare : cursiv::Extends<Value> {
  string_view name;
  Value* value{};
};
struct Query : cursiv::Extends<cursiv::AstNode> {
  vector<Value*> clauses;
};

// A digit run is a number and a word: the primary writes the number first, a
// comparison's value the word, and a similarity takes its number.
struct Clauses : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct TERM : terminal<some(cr<"0-9a-z.">)> {};
  struct NUMBER : terminal<some(d) + option(lit<"."> + some(d))> {};
  struct NameRule : rule<create<Name>(assign<&Name::text>(ref<TERM>))> {};
  struct NumberRule : rule<create<Number>(assign<&Number::text>(ref<NUMBER>))> {
  };
  struct FuzzySuffix
      : rule<create<Fuzzy>(
            lit<"~"> + option(assign<&Fuzzy::similarity>(ref<NumberRule>)))> {
  };
  struct TermRule
      : rule<create<Fuzzed>(
            assign<&Fuzzed::primary>(ref<NumberRule> | ref<NameRule>) +
            many(append<&Fuzzed::fuzzy>(ref<FuzzySuffix>)))> {};
  struct CompareRule
      : rule<create<Compare>(
            assign<&Compare::name>(ref<TERM>) + lit<"<"> +
            assign<&Compare::value>(ref<NameRule> | ref<NumberRule>))> {};
  struct FieldRule
      : rule<create<Field>(assign<&Field::name>(ref<TERM>) + lit<":"> +
                           assign<&Field::value>(ref<TermRule>))> {};
  struct Clause : rule<ref<CompareRule> | ref<FieldRule> | ref<TermRule>> {};
  struct Root
      : rule<create<Query>(some(append<&Query::clauses>(ref<Clause>))) + eof> {
  };
  using entry = Root;
  using ignore = WS;
};

// A signed digit run is a number and a bareword.

struct Stmts : cursiv::Extends<cursiv::AstNode> {
  vector<Value*> stmts;
};

// A blank run is trivia and a token of the same spelling: the token wins.
struct Blank : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct GAP : terminal<some(s)> {};
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct GapRule : rule<create<Name>(assign<&Name::text>(ref<GAP>))> {};
  struct WordRule : rule<create<Name>(assign<&Name::text>(ref<WORD>))> {};
  struct Root
      : rule<create<Stmts>(some(append<&Stmts::stmts>(ref<GapRule> |
                                                      ref<WordRule>))) +
             eof> {};
  using entry = Root;
  using ignore = WS;
};
struct BlankLl : Blank {};
struct BlankLr : Blank {};
struct Suite : Members {
  struct Root : rule<(lit<"cell:"> + ref<Cell::Root>) |
                     (lit<"ctor:"> + ref<CtorFirst::Root>) |
                     (lit<"method:"> + ref<MethodFirst::Root>) |
                     (lit<"clauses:"> + ref<Clauses::Root>)> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace tlllextie
