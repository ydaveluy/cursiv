#pragma once

// Optional positions a body cannot all distribute, each shape behind its own
// opening keyword: surplus clauses use nullable auxiliaries, which must keep
// end assertions in the body and add no conflict of their own.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace topt {

namespace ast {

struct Item : cursiv::Extends<cursiv::AstNode> {
  std::string_view v;
};

struct Opts : cursiv::Extends<cursiv::AstNode> {
  std::string_view a01, a02, a03, a04, a05;
  std::string_view a06, a07, a08, a09, a10;
  std::string_view a11, a12, a13, a14, a15;
  std::string_view a16, a17, a18, a19, a20;
  cursiv::vector<Item*> items;
};

struct Storey : cursiv::Extends<cursiv::AstNode> {
  std::string_view a01, a02, a03, a04;
};

struct Rec : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
  std::string_view a1;
  std::string_view a2;
  std::string_view a3;
  std::string_view a4;
  std::string_view a5;
  std::string_view a6;
  std::string_view a7;
  std::string_view a8;
  std::string_view nested;
};

struct Recs : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<Rec*> items;
};

struct VarDecl : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
  std::string_view from;
  std::string_view number;
};

struct Vars : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<VarDecl*> items;
};

}  // namespace ast

struct OptionalsG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct NUM : terminal<some(d)> {};

  // More optional positions than one body distributes. Distinct keywords and
  // a closing brace keep their reductions separate from shifts.
  struct ItemRule : rule<create<ast::Item>(assign<&ast::Item::v>(ref<ID>))> {};

  struct Block : rule<create<ast::Opts>(
      lit<"{"> +
            option(kw<"k01"> + assign<&ast::Opts::a01>(ref<ID>)) +
            option(kw<"k02"> + assign<&ast::Opts::a02>(ref<ID>)) +
            option(kw<"k03"> + assign<&ast::Opts::a03>(ref<ID>)) +
            option(kw<"k04"> + assign<&ast::Opts::a04>(ref<ID>)) +
            option(kw<"k05"> + assign<&ast::Opts::a05>(ref<ID>)) +
            option(kw<"k06"> + assign<&ast::Opts::a06>(ref<ID>)) +
            option(kw<"k07"> + assign<&ast::Opts::a07>(ref<ID>)) +
            option(kw<"k08"> + assign<&ast::Opts::a08>(ref<ID>)) +
            option(kw<"k09"> + assign<&ast::Opts::a09>(ref<ID>)) +
            option(kw<"k10"> + assign<&ast::Opts::a10>(ref<ID>)) +
            option(kw<"k11"> + assign<&ast::Opts::a11>(ref<ID>)) +
            option(kw<"k12"> + assign<&ast::Opts::a12>(ref<ID>)) +
            option(kw<"k13"> + assign<&ast::Opts::a13>(ref<ID>)) +
            option(kw<"k14"> + assign<&ast::Opts::a14>(ref<ID>)) +
            option(kw<"k15"> + assign<&ast::Opts::a15>(ref<ID>)) +
            option(kw<"k16"> + assign<&ast::Opts::a16>(ref<ID>)) +
            option(kw<"k17"> + assign<&ast::Opts::a17>(ref<ID>)) +
            option(kw<"k18"> + assign<&ast::Opts::a18>(ref<ID>)) +
            option(kw<"k19"> + assign<&ast::Opts::a19>(ref<ID>)) +
            option(kw<"k20"> + assign<&ast::Opts::a20>(ref<ID>)) +
            many(kw<"r1"> + append<&ast::Opts::items>(ref<ItemRule>)) +
            many(kw<"r2"> + append<&ast::Opts::items>(ref<ItemRule>)) +
            many(kw<"r3"> + append<&ast::Opts::items>(ref<ItemRule>)) +
            many(kw<"r4"> + append<&ast::Opts::items>(ref<ItemRule>)) +
            many(kw<"r5"> + append<&ast::Opts::items>(ref<ItemRule>)) +
            many(kw<"r6"> + append<&ast::Opts::items>(ref<ItemRule>)) +
            lit<"}">)> {};

  // Optional positions separated by choices containing an end assertion.
  struct StoreyBlock : rule<create<ast::Storey>(
      lit<"{"> +
      option(kw<"k01"> + assign<&ast::Storey::a01>(ref<ID>)) + (eof | lit<";">) +
      option(kw<"k02"> + assign<&ast::Storey::a02>(ref<ID>)) + (eof | lit<";">) +
      option(kw<"k03"> + assign<&ast::Storey::a03>(ref<ID>)) + (eof | lit<";">) +
      option(kw<"k04"> + assign<&ast::Storey::a04>(ref<ID>)) + (eof | lit<";">) +
      lit<"}">)> {};

  // An optional run ending its alternative, with a clause opening on the same
  // keyword as the next rule: the ambiguous suffix stays in the body.
  struct RecRule : rule<create<ast::Rec>(
      kw<"rec"> + assign<&ast::Rec::name>(ref<ID>) +
            option(kw<"k1"> + assign<&ast::Rec::a1>(ref<ID>)) +
            option(kw<"k2"> + assign<&ast::Rec::a2>(ref<ID>)) +
            option(kw<"k3"> + assign<&ast::Rec::a3>(ref<ID>)) +
            option(kw<"k4"> + assign<&ast::Rec::a4>(ref<ID>)) +
            option(kw<"k5"> + assign<&ast::Rec::a5>(ref<ID>)) +
            option(kw<"k6"> + assign<&ast::Rec::a6>(ref<ID>)) +
            option(kw<"k7"> + assign<&ast::Rec::a7>(ref<ID>)) +
            option(kw<"k8"> + assign<&ast::Rec::a8>(ref<ID>)) +
            option(kw<"rec"> + assign<&ast::Rec::nested>(ref<ID>)))> {};

  struct RecsRule : rule<create<ast::Recs>(
      some(append<&ast::Recs::items>(ref<RecRule>)))> {};

  // The recycling shape: an optional initialiser whose value is also an ID,
  // so a repair of one declaration can steal the keyword of the next.
  struct VarRule
      : rule<create<ast::VarDecl>(
            ikw<"var"> + assign<&ast::VarDecl::name>(ref<ID>) +
            option(lit<"="> + (assign<&ast::VarDecl::from>(ref<ID>) |
                               assign<&ast::VarDecl::number>(ref<NUM>))) +
            lit<";">)> {};

  struct VarsRule : rule<create<ast::Vars>(
                        some(append<&ast::Vars::items>(ref<VarRule>)))> {};

  struct Entry : rule<(kw<"optionals"> + ref<Block>) |
                      (kw<"storey"> + ref<StoreyBlock>) |
                      (kw<"entangled"> + ref<RecsRule>) |
                      (kw<"recycle"> + ref<VarsRule>)> {};

  using entry = Entry;
  using ignore = WS;
};

}  // namespace topt
