#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllconditions {

struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<cursiv::AstNode*> items;
};
struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Number : cursiv::Extends<Expression> {};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct StatementNode : cursiv::Extends<cursiv::AstNode> {
  Expression* value{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUMBER : terminal<some(d)> {};
  struct Atom : rule<create<Number>(ref<NUMBER>)> {};
  struct SameLineAdd : rule<!gap::line + lit<"+">> {};
  struct Sum : infix<Atom, Binary, left(ref<SameLineAdd>)> {};
  static constexpr auto Eos = implied(lit<";">, gap::line | &lit<"}"> | eof);
  struct Statement
      : rule<create<StatementNode>(
            (kw<"return"> + option(!gap::line + ref<ID>) + Eos) |
            (assign<&StatementNode::value>(ref<Sum>) + Eos) |
            (!kw<"reserved"> + ref<ID> +
             many(gap::none + lit<"."> + gap::none + ref<ID>) + Eos))> {};
  struct Block : rule<create<Document>(
                     lit<"{"> + many(append<&Document::items>(ref<Statement>)) +
                     lit<"}">)> {};
  struct Root
      : rule<create<Document>(
            many(append<&Document::items>(ref<Block> | ref<Statement>)) +
            eof)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};

struct Ll : Base {};
struct Lr : Base {};

struct TriviaBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // This visible reading is used only in the leading region. It must not
  // hide the grammar's ignored whitespace during later prediction.
  struct LeadingSpace : terminal<some(s)> {};
  struct GuardedName : rule<!kw<"reserved"> + ref<ID>> {};
  struct Names : rule<ref<GuardedName> + many(ref<GuardedName>)> {};
  struct Definition : rule<create<StatementNode>(
                          (ref<Names> + lit<"="> + ref<GuardedName>) |
                          (ref<Names> + lit<":"> + ref<GuardedName>))> {};
  struct KeywordDefinition : rule<create<StatementNode>(kw<"get"> + lit<"?">)> {
  };
  struct Root : rule<create<Document>(
                    option(tight(ref<LeadingSpace>)) +
                    many(append<&Document::items>(ref<Definition> |
                                                  ref<KeywordDefinition>) +
                         lit<";">) +
                    eof)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};
struct TriviaLl : TriviaBase {};
struct TriviaLr : TriviaBase {};

// The condition of an implied `;` reads more of the word after it than the
// token a prediction lexes there: `abd` refuses the insertion only at its
// third byte.
struct ReachBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Implied : rule<lit<"x"> + implied(lit<";">, &lit<"abc">)> {};
  struct Word : rule<lit<"abc">> {};
  struct Letters : rule<lit<"x"> + lit<"a"> + lit<"b"> + lit<"d">> {};
  struct Statement : rule<ref<Implied> | ref<Word> | ref<Letters>> {};
  struct Root : rule<create<Document>(many(ref<Statement>))> {};
  using entry = Root;
  using ignored = WS;
};
struct ReachLl : ReachBase {};

// An implied `;` whose condition holds on a token another reading takes
// normally: after a name, `#` continues `a #b?`, which blocks the insertion
// even where that reading fails later. After a number, nothing reads `#`.
struct RivalBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUMBER : terminal<some(d)> {};
  static constexpr auto Mark =
      lit<"#"> + ref<ID> + ref<ID> + ref<ID> + lit<"!">;
  static constexpr auto Eos = implied(lit<";">, &Mark);
  struct Statement
      : rule<create<StatementNode>(
            (ref<ID> + Eos) | (ref<NUMBER> + Eos) | Mark |
            (ref<ID> + lit<"#"> + ref<ID> + lit<"?">) | (ref<ID> + ref<ID>))> {
  };
  struct Root : rule<create<Document>(
                    many(append<&Document::items>(ref<Statement>)) + eof)> {};
  using entry = Root;
  using ignored = WS;
};
struct RivalLl : RivalBase {};
struct RivalLr : RivalBase {};

// A tag glued to its number: one creating group of `#`, a gap condition and
// an option, whose node rides onto the `#` where nothing follows it.
struct GluedBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  struct Atom : rule<create<Number>(ref<NUMBER>)> {};
  struct Tag : rule<create<StatementNode>(
                   lit<"#"> + gap::none +
                   option(assign<&StatementNode::value>(ref<Atom>)))> {};
  struct Root : rule<create<Document>(
                    many(append<&Document::items>(ref<Tag> | ref<Atom>)) +
                    eof)> {};
  using entry = Root;
  using ignored = WS;
};
struct GluedLl : GluedBase {};

}  // namespace tllconditions
