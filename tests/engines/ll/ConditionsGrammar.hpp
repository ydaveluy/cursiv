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
  struct Sum : infix<Atom, &Binary::left, &Binary::op, &Binary::right,
                     left(ref<SameLineAdd>)> {};
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
  using ignore = WS;
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
  using ignore = WS;
  using hidden = COMMENT;
};
struct TriviaLl : TriviaBase {};
struct TriviaLr : TriviaBase {};

}  // namespace tllconditions
