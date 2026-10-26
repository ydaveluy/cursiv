#pragma once

// The ambiguous shapes of the torture lane, one opening keyword each: the
// cases share no state and each owns exactly one conflict. Rules that read
// alike stay distinct: a rank follows the order rules are first reached in,
// which a shared rule would carry from one case into the next.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "torture/DanglingAst.hpp"
#include "torture/DeepIfGrammar.hpp"
#include "torture/ElectionAst.hpp"

namespace telect {

namespace ast {
struct Case : cursiv::Extends<cursiv::AstNode> {
  Top* top = nullptr;
  Hold* hold = nullptr;
  tdang::ast::Stmt* stmt = nullptr;
  List* list = nullptr;
};
}  // namespace ast

struct Election : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct ID : terminal<some(cr<"a-z0-9">)> {};
  struct A : terminal<lit<"a">> {};

  // Two distinct rules: the earlier complete reading wins, while the other
  // reduction remains available if the earlier one fails.
  struct RankXYX : rule<create<ast::XN>(kw<"a"> + kw<"b">)> {};
  struct RankXYY : rule<create<ast::YN>(kw<"b">)> {};
  struct RankXY
      : rule<create<ast::Top>(assign<&ast::Top::x>(ref<RankXYX>) + kw<"x">) |
             create<ast::Top>(kw<"a"> + assign<&ast::Top::y>(ref<RankXYY>) +
                              kw<"x">)> {};
  struct RankYXY : rule<create<ast::YN>(kw<"b">)> {};
  struct RankYXX : rule<create<ast::XN>(kw<"a"> + kw<"b">)> {};
  struct RankYX
      : rule<create<ast::Top>(kw<"a"> + assign<&ast::Top::y>(ref<RankYXY>) +
                              kw<"x">) |
             create<ast::Top>(assign<&ast::Top::x>(ref<RankYXX>) + kw<"x">)> {};

  // Two ordered alternatives of one rule: the conflict survives to the table
  // and the episode forks over it.
  struct RunXYR
      : rule<create<ast::RX>(kw<"c"> + kw<"a">) | create<ast::RY>(kw<"a">)> {};
  struct RunXY
      : rule<create<ast::Hold>(assign<&ast::Hold::r>(ref<RunXYR>) + kw<"x">) |
             create<ast::Hold>(kw<"c"> + assign<&ast::Hold::r>(ref<RunXYR>) +
                               kw<"x">)> {};
  struct RunYXR
      : rule<create<ast::RY>(kw<"a">) | create<ast::RX>(kw<"c"> + kw<"a">)> {};
  struct RunYX
      : rule<create<ast::Hold>(assign<&ast::Hold::r>(ref<RunYXR>) + kw<"x">) |
             create<ast::Hold>(kw<"c"> + assign<&ast::Hold::r>(ref<RunYXR>) +
                               kw<"x">)> {};

  // A reading a condition guards against one that shifts the same token:
  // the condition must not change who wins, and without it the arm written
  // first does.
  struct GuardXYName : rule<!kw<"x"> + ref<ID>> {};
  struct GuardXYX : rule<create<ast::XN>(ref<GuardXYName> + ref<ID>)> {};
  struct GuardXYY : rule<create<ast::YN>(ref<ID> + ref<ID>)> {};
  struct GuardXY
      : rule<create<ast::Top>(assign<&ast::Top::x>(ref<GuardXYX>) + kw<"x">) |
             create<ast::Top>(assign<&ast::Top::y>(ref<GuardXYY>) + kw<"x">)> {
  };
  struct GuardYXName : rule<!kw<"x"> + ref<ID>> {};
  struct GuardYXY : rule<create<ast::YN>(ref<ID> + ref<ID>)> {};
  struct GuardYXX : rule<create<ast::XN>(ref<GuardYXName> + ref<ID>)> {};
  struct GuardYX
      : rule<create<ast::Top>(assign<&ast::Top::y>(ref<GuardYXY>) + kw<"x">) |
             create<ast::Top>(assign<&ast::Top::x>(ref<GuardYXX>) + kw<"x">)> {
  };

  // Qual shifts the token the name's condition reads, from X's own side.
  // Without the condition, Name and Y reduce side by side and the one
  // declared first wins: the condition must not change that.
  struct MixedXYName : rule<!kw<"x"> + ref<ID>> {};
  struct MixedXYQual : rule<ref<ID> + lit<"."> + ref<ID>> {};
  struct MixedXYX
      : rule<create<ast::XN>((ref<MixedXYQual> | ref<MixedXYName>)+ref<ID>)> {};
  struct MixedXYY : rule<create<ast::YN>(ref<ID>)> {};
  struct MixedXY
      : rule<create<ast::Top>(assign<&ast::Top::x>(ref<MixedXYX>) + kw<"x">) |
             create<ast::Top>(assign<&ast::Top::y>(ref<MixedXYY>) + ref<ID> +
                              kw<"x">)> {};
  struct MixedYXY : rule<create<ast::YN>(ref<ID>)> {};
  struct MixedYXName : rule<!kw<"x"> + ref<ID>> {};
  struct MixedYXQual : rule<ref<ID> + lit<"."> + ref<ID>> {};
  struct MixedYXX
      : rule<create<ast::XN>((ref<MixedYXQual> | ref<MixedYXName>)+ref<ID>)> {};
  struct MixedYX
      : rule<create<ast::Top>(assign<&ast::Top::y>(ref<MixedYXY>) + ref<ID> +
                              kw<"x">) |
             create<ast::Top>(assign<&ast::Top::x>(ref<MixedYXX>) + kw<"x">)> {
  };

  // The dangling else with its arms in the opposite order to `DeepIf`'s: the
  // outer if keeps its first arm, the one with an else.
  struct DeepIfRev
      : rule<
            create<tdang::ast::Stmt>(
                kw<"if"> + assign<&tdang::ast::Stmt::cond>(ref<ID>) +
                kw<"then"> + assign<&tdang::ast::Stmt::then_>(ref<DeepIfRev>) +
                kw<"else"> + assign<&tdang::ast::Stmt::else_>(ref<DeepIfRev>)) |
            create<tdang::ast::Stmt>(
                kw<"if"> + assign<&tdang::ast::Stmt::cond>(ref<ID>) +
                kw<"then"> + assign<&tdang::ast::Stmt::then_>(ref<DeepIfRev>)) |
            create<tdang::ast::Stmt>(
                assign<&tdang::ast::Stmt::cond>(ref<ID>))> {};

  // A run of "a"s groupable into pairs or triples: the width comes from
  // partitioning the same run, not from a shift/reduce choice.
  struct OverlapGroup : rule<create<ast::Pair>(ref<A> + ref<A>) |
                             create<ast::Triple>(ref<A> + ref<A> + ref<A>)> {};
  struct Overlap : rule<create<ast::List>(
                       some(append<&ast::List::groups>(ref<OverlapGroup>)))> {};

  struct Entry
      : rule<create<ast::Case>(kw<"rankxy"> +
                               assign<&ast::Case::top>(ref<RankXY>)) |
             create<ast::Case>(kw<"rankyx"> +
                               assign<&ast::Case::top>(ref<RankYX>)) |
             create<ast::Case>(kw<"runxy"> +
                               assign<&ast::Case::hold>(ref<RunXY>)) |
             create<ast::Case>(kw<"runyx"> +
                               assign<&ast::Case::hold>(ref<RunYX>)) |
             create<ast::Case>(kw<"guardxy"> +
                               assign<&ast::Case::top>(ref<GuardXY>)) |
             create<ast::Case>(kw<"guardyx"> +
                               assign<&ast::Case::top>(ref<GuardYX>)) |
             create<ast::Case>(kw<"mixedxy"> +
                               assign<&ast::Case::top>(ref<MixedXY>)) |
             create<ast::Case>(kw<"mixedyx"> +
                               assign<&ast::Case::top>(ref<MixedYX>)) |
             create<ast::Case>(kw<"deepifrev"> +
                               assign<&ast::Case::stmt>(ref<DeepIfRev>)) |
             create<ast::Case>(kw<"overlap"> +
                               assign<&ast::Case::list>(ref<Overlap>)) |
             ref<tdang::DeepIf::Stmt>> {};

  using entry = Entry;
  using ignore = WS;
};

}  // namespace telect
