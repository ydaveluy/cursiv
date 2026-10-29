// Two complete readings of one text that write different members: the first
// decision the text meets decides, its arm written first winning.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tdeferred {

struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Ident : cursiv::Extends<Expr> {
  string_view text;
};
struct Select : cursiv::Extends<Expr> {
  Expr* prefix;
  string_view text;
};
struct Range : cursiv::Extends<Expr> {
  Expr* low;
  Expr* high;
};
struct Subtype : cursiv::Extends<Expr> {
  string_view mark;
  Expr* constraint;
};
struct Param : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
  Expr* type;
  Expr* def;
};
struct Quant : cursiv::Extends<Expr> {
  boolean all;
  Param* var;
  Expr* predicate;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Expr* value;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Comment : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-zA-Z">)> {};
  using ignored = list<WS, Comment>;
  struct QualName : rule<some(ref<ID>, lit<".">)> {};
  struct Text : rule<ref<QualName> + option(lit<"'"> + ikw<"class">)> {};
  struct IdentRule : rule<assign<&Ident::text>(ref<ID>)> {};
  struct Name : rule<(ref<IdentRule> |
                      create<Ident>(assign<&Ident::text>(lit<"'x'">))) +
                     many(nest<Select, &Select::prefix>(
                         lit<"."> + assign<&Select::text>(ref<ID>)))> {};
  struct RangeRule
      : rule<create<Range>(assign<&Range::low>(ref<Name>) + lit<".."> +
                           assign<&Range::high>(ref<Name>))> {};
  struct PlainSubtype
      : rule<create<Subtype>(assign<&Subtype::mark>(ref<Text>) +
                             option(lit<"range"> + assign<&Subtype::constraint>(
                                                       ref<RangeRule>)))> {};
  struct Discrete : rule<ref<RangeRule> | ref<PlainSubtype>> {};
  static constexpr auto quantifier =
      flag<&Quant::all>(ikw<"all">) | ikw<"some">;
  static constexpr auto qtype = create<Quant>(
      ikw<"for"> + quantifier +
      assign<&Quant::var>(create<Param>(append<&Param::names>(ref<ID>) +
                                        ikw<"in"> + option(ikw<"reverse">) +
                                        assign<&Param::type>(ref<Discrete>))) +
      lit<"=>"> + assign<&Quant::predicate>(ref<Name>));
  static constexpr auto qof = create<Quant>(
      ikw<"for"> + quantifier +
      assign<&Quant::var>(create<Param>(
          append<&Param::names>(ref<ID>) +
          option(lit<":"> + assign<&Param::type>(ref<PlainSubtype>)) +
          ikw<"of"> + option(ikw<"reverse">) +
          assign<&Param::def>(ref<Name>))) +
      lit<"=>"> + assign<&Quant::predicate>(ref<Name>));
  static constexpr auto qdef = create<Quant>(
      ikw<"for"> + quantifier +
      assign<&Quant::var>(create<Param>(append<&Param::names>(ref<ID>) +
                                        ikw<"in"> + option(ikw<"reverse">) +
                                        assign<&Param::def>(ref<Name>))) +
      lit<"=>"> + assign<&Quant::predicate>(ref<Name>));

};

struct NestedFirst : Base {
  struct Choice : rule<qtype | qof | qdef> {};
  struct Root : rule<create<Doc>(lit<"X"> + lit<":="> + lit<"("> +
                                 assign<&Doc::value>(ref<Choice>) + lit<")"> +
                                 lit<";"> + eof)> {};
  using entry = Root;
};

}  // namespace tdeferred
