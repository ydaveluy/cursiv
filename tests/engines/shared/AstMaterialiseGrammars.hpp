#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include <cstdint>
#include <string_view>
#include <variant>
// Shapes the generator accepts: each must build the tree its directives
// describe, in both engines.

namespace tmaterialise {

struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Var : cursiv::Extends<Expr> {
  string_view name;
};
struct Num : cursiv::Extends<Expr> {
  int64 value = 0;
};
struct Prop : cursiv::Extends<Expr> {
  Expr* object = nullptr;
  string_view name;
};
struct Index : cursiv::Extends<Expr> {
  Expr* object = nullptr;
  Expr* index = nullptr;
};
struct Neg : cursiv::Extends<Expr> {
  Expr* operand = nullptr;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct Cast : cursiv::Extends<Expr> {
  Expr* operand = nullptr;
  string_view type;
};
struct Inst : cursiv::Extends<Expr> {
  Expr* operand = nullptr;
  string_view type;
};
struct Assign : cursiv::Extends<Expr> {
  Var* target = nullptr;
  Expr* value = nullptr;
};
struct Pair : cursiv::Extends<Expr> {
  Expr* first = nullptr;
  Expr* second = nullptr;
};
struct Target : cursiv::Extends<Expr> {
  Expr* property = nullptr;
};
struct Row : cursiv::Extends<cursiv::AstNode> {
  vector<variant<std::monostate, int64, Row*>> items;
};
using Atom = std::variant<std::string_view, Expr*>;
struct Block : cursiv::Extends<cursiv::AstNode> {
  vector<Atom> atoms;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Expr* value = nullptr;
  vector<Expr*> children;
  boolean flag;
};
struct Flags : cursiv::Extends<cursiv::AstNode> {
  boolean on = false;
};
struct Box : cursiv::Extends<Flags> {
  Flags* in = nullptr;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee = nullptr;
  vector<Expr*> args;
  boolean flagged = false;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<as<std::int64_t>(some(d))> {};
  struct WORD : terminal<some(cr<"A-Z">)> {};
  struct VarBase : rule<lit<"$"> + assign<&Var::name>(ref<ID>)> {};
  struct NumRule : rule<assign<&Num::value>(ref<NUM>)> {};
  struct Primary : rule<ref<VarBase> | ref<NumRule>> {};
  static constexpr auto step =
      nest<Prop, &Prop::object>(lit<"."> + assign<&Prop::name>(ref<ID>)) |
      nest<Index, &Index::object>(
          lit<"["> + assign<&Index::index>(ref<NumRule>) + lit<"]">);
};

template <unsigned N>
inline constexpr auto stacked_flags = cursiv::nest<Box, &Box::in>(
    stacked_flags<N - 1>);
template <>
inline constexpr auto stacked_flags<0> = cursiv::kw<"x">;

struct StackedFlags : Base {
  struct Root : rule<create<Flags>(
                         ref<ID> + flag<&Flags::on>(stacked_flags<20>))> {};
};

struct AtomChain : Base {
  struct VarChain : rule<ref<VarBase> + many(step)> {};
  struct Root : rule<create<Block>(
                    many(append<&Block::atoms>(ref<WORD> | ref<VarChain>)))> {};
  using ignored = WS;
};

struct FlankedItems : Base {
  struct WS : terminal<some(cr<" \t">)> {};
  struct NL : terminal<lit<"\n">> {};
  struct List
      : rule<create<Row>(
            lit<"["> + many(ref<NL>) +
            some(append<&Row::items>(ref<NUM> | ref<List>) + many(ref<NL>),
                 lit<","> + many(ref<NL>)) +
            lit<"]">)> {};
  using entry = List;
  using ignored = WS;
};

struct SharedPrefix : Base {
  struct Unary
      : rule<create<Neg>(lit<"-"> + assign<&Neg::operand>(ref<Unary>)) |
             (ref<Primary> + many(step))> {};
  using ignored = WS;
};

struct PayloadCreate : Base {
  struct Root
      : rule<create<Holder>(
            assign<&Holder::value>(create<Var>(assign<&Var::name>(ref<ID>)) +
                                   many(step)) +
            many(lit<";"> + append<&Holder::children>(
                                create<Var>(assign<&Var::name>(ref<ID>)) +
                                option(step))))> {};
  using ignored = WS;
};

struct FlagGroup : Base {
  struct Root : rule<create<Holder>(
                    option(flag<&Holder::flag>(lit<"!">) +
                           assign<&Holder::value>(ref<VarBase>)) +
                    many(lit<";"> + append<&Holder::children>(ref<VarBase>)))> {
  };
  using ignored = WS;
};

// A call's argument can itself close over a call. The argument write and the
// following flag still belong to the outer call, even when the inner rule
// returns their common base class and all three effects share one record.
struct RecursiveCalls : Base {
  struct Expression
      : rule<ref<VarBase> + option(nest<Call, &Call::callee>(
                               lit<":"> + flag<&Call::flagged>(
                                   some(append<&Call::args>(ref<Expression>),
                                        lit<",">))))> {};
  using ignored = WS;
};

struct OptionalNest : Base {
  struct Sum;
  struct Unary
      : rule<create<Neg>(lit<"-"> + assign<&Neg::operand>(ref<Unary>)) |
             ref<Primary> | (lit<"("> + ref<Sum> + lit<")">)> {};
  struct Castable
      : rule<ref<Unary> +
             option(nest<Cast, &Cast::operand>(
                 kw<"cast"> + kw<"as"> + assign<&Cast::type>(ref<ID>)))> {};
  struct Instance
      : rule<ref<Castable> +
             option(nest<Inst, &Inst::operand>(
                 kw<"instance"> + kw<"of"> + assign<&Inst::type>(ref<ID>)))> {};
  struct Sum : infix<Instance, Bin, (left(lit<"+"> | lit<"-">))> {};
  using ignored = WS;
};

struct PassThrough : Base {
  struct Expression;
  struct Ident : rule<assign<&Var::name>(ref<ID>)> {};
  struct ParenExpr : rule<lit<"("> + ref<Expression> + lit<")">> {};
  struct Term : rule<ref<Ident> | ref<NumRule> | ref<ParenExpr>> {};
  struct Sum : infix<Term, Bin, (left(lit<"+"> | lit<"-">))> {};
  struct Test
      : rule<ref<Sum> +
             option(nest<Bin, &Bin::left>(assign<&Bin::op>(lit<"<">) +
                                          assign<&Bin::right>(ref<Sum>)))> {};
  struct Expression
      : rule<create<Assign>(assign<&Assign::target>(ref<Ident>) + lit<"="> +
                            assign<&Assign::value>(ref<Expression>)) |
             ref<Test>> {};
  using ignored = WS;
};

struct Relay : Base {
  struct Operand;
  struct Sum : infix<Operand, Bin, left(lit<"+">)> {};
  struct Operand
      : rule<create<Neg>(lit<"!"> + assign<&Neg::operand>(ref<Operand>)) |
             (lit<"("> + ref<Sum> + lit<")">) |
             create<Pair>(lit<"["> + assign<&Pair::first>(ref<Sum>) + lit<","> +
                          assign<&Pair::second>(ref<Sum>) + lit<"]">) |
             ref<VarBase>> {};
  struct Root : rule<ref<Sum>> {};
  using ignored = WS;
};

struct UnassignedTerminal : Base {
  struct NewTarget : rule<create<Target>(kw<"new"> + lit<"."> + ref<ID>)> {};
  struct Named : rule<create<Var>(kw<"fn"> + assign<&Var::name>(ref<ID>))> {};
  struct Root : rule<ref<NewTarget> | ref<Named> | ref<VarBase>> {};
  using ignored = WS;
};

// A rule that passes on the node of the rule it calls, behind a condition no
// repair can make hold: a repair gives the call up, and the rule has no node
// to pass on.
struct GivenUp : Base {
  struct Named : rule<create<Var>(lit<"n"> + assign<&Var::name>(ref<ID>) +
                                  &lit<"!">)> {};
  struct Passed : rule<ref<Named> + option(lit<"?">)> {};
  struct Root : rule<create<Holder>(lit<"x"> +
                                    assign<&Holder::value>(ref<Passed>) +
                                    option(lit<"!">) + lit<";">)> {};
  using ignored = WS;
};

struct Suite : Base {
  struct Root : rule<(lit<"atomchain:"> + ref<AtomChain::Root>) |
                     (lit<"sharedprefix:"> + ref<SharedPrefix::Unary>) |
                     (lit<"payloadcreate:"> + ref<PayloadCreate::Root>) |
                     (lit<"flaggroup:"> + ref<FlagGroup::Root>) |
                     (lit<"recursivecalls:"> + ref<RecursiveCalls::Expression>) |
                     (lit<"optionalnest:"> + ref<OptionalNest::Sum>) |
                     (lit<"passthrough:"> + ref<PassThrough::Expression>) |
                     (lit<"relay:"> + ref<Relay::Root>) |
                     (lit<"unassignedterminal:"> + ref<UnassignedTerminal::Root>) |
                     (lit<"givenup:"> + ref<GivenUp::Root>) |
                     (lit<"passed:"> + ref<GivenUp::Passed>) |
                     (lit<"stacked:"> + ref<StackedFlags::Root>)> {};
  using entry = Root;
  using ignored = WS;
};
// The derivation model cannot keep an outer node around a create inside a
// directive's payload, so its fuzz lanes leave PayloadCreate out; that shape is
// checked against Holder and its children instead.
struct FuzzSuite : Suite {
  struct Root : rule<(lit<"atomchain:"> + ref<AtomChain::Root>) |
                     (lit<"sharedprefix:"> + ref<SharedPrefix::Unary>) |
                     (lit<"flaggroup:"> + ref<FlagGroup::Root>) |
                     (lit<"optionalnest:"> + ref<OptionalNest::Sum>) |
                     (lit<"passthrough:"> + ref<PassThrough::Expression>) |
                     (lit<"relay:"> + ref<Relay::Root>) |
                     (lit<"unassignedterminal:"> + ref<UnassignedTerminal::Root>)> {};
  struct Entry : rule<lit<"mat:"> + ref<Root>> {};
  using entry = Entry;
};

}  // namespace tmaterialise

// A rule that calls another and nests behind it: the node the call made is
// what the nest wraps, whether the nest may repeat or must stand once.

namespace tnestadopt {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Ident : cursiv::Extends<Expr> {
  string_view name;
};
struct Number : cursiv::Extends<Expr> {
  string_view text;
};
struct Call : cursiv::Extends<Expr> {
  Expr* target = nullptr;
  vector<Expr*> args;
};
struct Selector : cursiv::Extends<Expr> {
  Expr* object = nullptr;
  string_view field;
};
struct Index : cursiv::Extends<Expr> {
  Expr* array = nullptr;
  Expr* index = nullptr;
};
struct Assign : cursiv::Extends<Expr> {
  Expr* target = nullptr;
  Expr* value = nullptr;
};
struct Stmt : cursiv::Extends<cursiv::AstNode> {
  Expr* value = nullptr;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};

  struct Name : rule<assign<&ast::Ident::name>(ref<ID>)> {};
  struct Number : rule<assign<&ast::Number::text>(ref<NUM>)> {};
  struct Primary : rule<ref<Name> | ref<Number>> {};

  static constexpr auto Step = nest<ast::Index, &ast::Index::array>(
      lit<"["> + assign<&ast::Index::index>(ref<Name>) + lit<"]">);

  struct One : rule<ref<Name> + Step> {};
  struct Some : rule<ref<Name> + some(Step)> {};
  struct Chosen : rule<ref<Some> | ref<Name>> {};

  struct Assigned : rule<create<ast::Assign>(
                        assign<&ast::Assign::target>(ref<Chosen>) + lit<"="> +
                        assign<&ast::Assign::value>(ref<Name>))> {};

  struct Semi : rule<ref<Name> + nest<ast::Stmt, &ast::Stmt::value>(lit<";">)> {
  };

  static constexpr auto short_call =
      nest<&ast::Call::target>(lit<"("> + lit<")">);
  static constexpr auto selector = nest<&ast::Selector::object>(
      lit<"."> + assign<&ast::Selector::field>(ref<ID>));

  struct NullableCall
      : rule<ref<Name> +
             nest<&ast::Call::target>(option(lit<"("> + lit<")">))> {};
  struct EmptyHeadCall : rule<create<ast::Ident> + short_call> {};
  struct GuardedEmptyHeadCall
      : rule<create<ast::Ident> + &lit<"("> + short_call> {};
  struct NestedPayload
      : rule<ref<Name> + nest<&ast::Selector::object>(
                             lit<"."> +
                             assign<&ast::Selector::field>(ref<ID>) +
                             short_call)> {};

  struct ShortAdopted : rule<ref<Name> + option(short_call) +
                             many(selector + option(short_call))> {};
  struct ShortCreated
      : rule<create<ast::Ident>(assign<&ast::Ident::name>(ref<ID>)) +
             option(short_call) + many(selector + option(short_call))> {};
  struct ShortHead : rule<ref<Name> + option(short_call)> {};
  struct ShortRecursive
      : rule<(ref<ShortRecursive> + selector + option(short_call)) |
             ref<ShortHead>> {};

  // The pure-adoption arm is part of the indirect cycle, not an optional
  // suffix factored out of it. Both call and selector wrappers keep a prefix.
  struct ShortExpression;
  struct ShortSelected : rule<(ref<ShortExpression> + selector) | ref<Name>> {};
  struct ShortExpression
      : rule<(ref<ShortSelected> + short_call) | ref<ShortSelected>> {};

  static constexpr auto index_body =
      lit<"["> + assign<&ast::Index::index>(ref<Primary>) + lit<"]">;
  static constexpr auto call_body =
      lit<"("> + option(append<&ast::Call::args>(ref<Primary>)) + lit<")">;
  static constexpr auto index_tail = nest<&ast::Index::array>(index_body);
  static constexpr auto call_tail = nest<&ast::Call::target>(call_body);
  struct IndexDirect : rule<ref<Primary> + many(index_tail)> {};
  struct TailChoice : rule<ref<Primary> + many(call_tail | index_tail)> {};
  struct TailOuterGuard
      : rule<ref<Primary> + many(gap::none + (call_tail | index_tail))> {};
  struct TailInnerGuard
      : rule<ref<Primary> +
             many(nest<&ast::Call::target>(gap::none + call_body) |
                  nest<&ast::Index::array>(gap::none + index_body))> {};

  struct Root
      : rule<
            (kw<"one"> + ref<One>) | (kw<"some"> + ref<Some>) |
            (kw<"assign"> + ref<Assigned>) | (kw<"semi"> + ref<Semi>) |
            (kw<"nullablenest"> + assign<&ast::Stmt::value>(ref<NullableCall>)) |
            (kw<"emptyhead"> + assign<&ast::Stmt::value>(ref<EmptyHeadCall>)) |
            (kw<"guardedempty"> +
             assign<&ast::Stmt::value>(ref<GuardedEmptyHeadCall>)) |
            (kw<"nestedpayload"> +
             assign<&ast::Stmt::value>(ref<NestedPayload>)) |
            (kw<"shortadopt"> + assign<&ast::Stmt::value>(ref<ShortAdopted>)) |
            (kw<"shortcreate"> + assign<&ast::Stmt::value>(ref<ShortCreated>)) |
            (kw<"shortleft"> + assign<&ast::Stmt::value>(ref<ShortRecursive>)) |
            (kw<"shortcycle"> +
             assign<&ast::Stmt::value>(ref<ShortExpression>)) |
            (kw<"indexdirect"> + assign<&ast::Stmt::value>(ref<IndexDirect>)) |
            (kw<"tailchoice"> + assign<&ast::Stmt::value>(ref<TailChoice>)) |
            (kw<"tailouter"> + assign<&ast::Stmt::value>(ref<TailOuterGuard>)) |
            (kw<"tailinner"> +
             assign<&ast::Stmt::value>(ref<TailInnerGuard>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tnestadopt

namespace tmaterialise {
struct Combined : Suite {
  struct Entry : rule<ref<Suite::Root> |
                      (lit<"flanked:"> + skip(ignore<FlankedItems::WS>, hide<>,
                          ref<FlankedItems::List> + eof)) |
                      (lit<"adopt:"> + ref<tnestadopt::Base::Root>)> {};
  using entry = Entry;
};
}  // namespace tmaterialise
