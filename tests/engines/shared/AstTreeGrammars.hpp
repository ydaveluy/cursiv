#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "AstMaterialiseGrammars.hpp"
#include "DirectiveSequenceGrammar.hpp"
#include <cstdint>
#include <string_view>
// The tree grammar.md states, in both engines: the node a rule returns, the
// operand of a directive building its value apart, an adopted node taking the
// writes made before it.

namespace ttree {

namespace ast {
struct Variable : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  std::int64_t value = 0;
};
struct Module : cursiv::Extends<cursiv::AstNode> {
  vector<Variable*> variables;
};

struct Decl : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  boolean exported = false;
};
struct Function : cursiv::Extends<Decl> {};
struct Fn : cursiv::Extends<Decl> {};
struct Var : cursiv::Extends<Decl> {
  std::int64_t value = 0;
};

struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expression> {
  string_view name;
};
struct Number : cursiv::Extends<Expression> {
  std::int64_t value = 0;
};
struct Index : cursiv::Extends<Expression> {
  Expression* array = nullptr;
  Expression* index = nullptr;
};

struct B : cursiv::Extends<cursiv::AstNode> {
  string_view n;
};
struct A : cursiv::Extends<cursiv::AstNode> {
  B* m = nullptr;
  vector<B*> items;
};

struct C : cursiv::Extends<cursiv::AstNode> {
  string_view n;
};
struct D : cursiv::Extends<cursiv::AstNode> {
  C* c = nullptr;
};
struct E : cursiv::Extends<cursiv::AstNode> {
  D* d = nullptr;
};

struct Leaf : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Pair : cursiv::Extends<cursiv::AstNode> {
  Leaf* leaf = nullptr;
};

struct Block : cursiv::Extends<cursiv::AstNode> {
  vector<Expression*> atoms;
};
}  // namespace ast

struct QuickStart : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"//">, &eol | eof)> {};
  struct ID
      : terminal<(uc::XID_Start | cr<"_">)+many(uc::XID_Continue | cr<"_">)> {};
  struct INTEGER : terminal<some(d), std::int64_t> {};

  using ignore = WS;
  using hidden = COMMENT;
  static constexpr auto word_boundary = uc::XID_Continue | cr<"_">;

  struct VariableRule
      : rule<kw<"let"> + assign<&ast::Variable::name>(ref<ID>) + lit<"="> +
             assign<&ast::Variable::value>(ref<INTEGER>) + lit<";">> {};
  struct Document
      : rule<many(append<&ast::Module::variables>(ref<VariableRule>))> {};

  using entry = Document;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d), std::int64_t> {};
  using ignore = WS;
};

struct Declared : Base {
  struct Root : rule<assign<&ast::Decl::name>(ref<ID>) +
                     create<ast::Function>(lit<"()">)> {};
};

struct Exported : Base {
  static constexpr auto fn_body = kw<"fn"> + assign<&ast::Decl::name>(ref<ID>);
  static constexpr auto var_body =
      kw<"var"> + assign<&ast::Decl::name>(ref<ID>) + lit<"="> +
      assign<&ast::Var::value>(ref<NUM>);
  struct Root : rule<option(flag<&ast::Decl::exported>(kw<"export">)) +
                     (create<ast::Fn>(fn_body) | create<ast::Var>(var_body))> {
  };
};

struct Indexed : Base {
  struct NameRule : rule<assign<&ast::Name::name>(ref<ID>)> {};
  struct NumberRule : rule<assign<&ast::Number::value>(ref<NUM>)> {};
  struct Expression : rule<ref<NameRule> | ref<NumberRule>> {};
  struct Root
      : rule<ref<NameRule> +
             many(nest<&ast::Index::array>(
                 lit<"["> + assign<&ast::Index::index>(ref<Expression>) +
                 lit<"]">))> {};
};

struct Apart : Base {
  struct Root : rule<assign<&ast::A::m>(assign<&ast::B::n>(ref<ID>))> {};
};

struct PerTurn : Base {
  struct Root
      : rule<many(append<&ast::A::items>(assign<&ast::B::n>(ref<ID>)))> {};
};

struct Present : Base {
  struct Root : rule<assign<&ast::A::m>(option(assign<&ast::B::n>(ref<ID>)))> {
  };
};

struct Absent : Base {
  struct Root : rule<option(assign<&ast::A::m>(assign<&ast::B::n>(ref<ID>)))> {
  };
};

struct Nested : Base {
  struct Root : rule<assign<&ast::E::d>(
                    assign<&ast::D::c>(assign<&ast::C::n>(ref<ID>)))> {};
};


struct Adopted : Base {
  struct LeafRule : rule<create<ast::Leaf>(lit<"$">)> {};
  struct Root : rule<assign<&ast::Pair::leaf>(
                    ref<LeafRule> + assign<&ast::Leaf::name>(ref<ID>))> {};
};

struct Chained : Base {
  struct NameRule : rule<assign<&ast::Name::name>(ref<ID>)> {};
  struct Root
      : rule<many(append<&ast::Block::atoms>(
            ref<NameRule> +
            many(nest<&ast::Index::array>(
                lit<"."> + assign<&ast::Index::index>(ref<NameRule>)))))> {};
};


struct Suite : Base {
  struct Root : rule<(lit<"declared:"> + ref<Declared::Root>) |
                     (lit<"exported:"> + ref<Exported::Root>) |
                     (lit<"indexed:"> + ref<Indexed::Root>) |
                     (lit<"apart:"> + ref<Apart::Root>) |
                     (lit<"perturn:"> + ref<PerTurn::Root>) |
                     (lit<"present:"> + ref<Present::Root>) |
                     (lit<"absent:"> + ref<Absent::Root>) |
                     (lit<"nested:"> + ref<Nested::Root>) |
                     (lit<"adopted:"> + ref<Adopted::Root>) |
                     (lit<"chained:"> + ref<Chained::Root>)> {};
  using entry = Root;
};
}  // namespace ttree

// Two grammars that write the same members, their arms in opposite orders and
// each member first named in a different place.

namespace tmember {

namespace ast {
struct Leaf : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Base : cursiv::Extends<cursiv::AstNode> {
  Leaf* first = nullptr;
  reference<Leaf> link;
  Leaf* second = nullptr;
};
struct Derived : cursiv::Extends<Base> {
  Leaf* third = nullptr;
  vector<Leaf*> items;
};
struct Unnamed : cursiv::Extends<Derived> {};
}  // namespace ast

struct Parts : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct LeafR : rule<create<ast::Leaf>(assign<&ast::Leaf::name>(ref<ID>))> {};
};

struct Forward : Parts {
  struct Root : rule<create<ast::Derived>(
                    (kw<"f"> + assign<&ast::Base::first>(ref<LeafR>)) &
                    (kw<"l"> + assign<&ast::Base::link>(ref<ID>)) &
                    (kw<"s"> + assign<&ast::Base::second>(ref<LeafR>)) &
                    (kw<"t"> + assign<&ast::Derived::third>(ref<LeafR>)) &
                    some(kw<"i"> + append<&ast::Derived::items>(ref<LeafR>)))> {
  };
  using entry = Root;
  using ignore = WS;
};

struct Backward : Parts {
  struct Root : rule<create<ast::Derived>(
                    some(kw<"i"> + append<&ast::Derived::items>(ref<LeafR>)) &
                    (kw<"t"> + assign<&ast::Derived::third>(ref<LeafR>)) &
                    (kw<"s"> + assign<&ast::Base::second>(ref<LeafR>)) &
                    (kw<"l"> + assign<&ast::Base::link>(ref<ID>)) &
                    (kw<"f"> + assign<&ast::Base::first>(ref<LeafR>)))> {};
  using entry = Root;
  using ignore = WS;
};

struct BackwardLr : Backward {};

}  // namespace tmember

namespace tcyclicadopt {

struct Base : cursiv::Extends<cursiv::AstNode> {};
struct Element : cursiv::Extends<Base> {
  boolean parenthesized;
};
struct Leaf : cursiv::Extends<Element> {
  string_view value;
};
struct Counted : cursiv::Extends<Element> {
  Base* element = nullptr;
  string_view cardinality;
};
struct Group : cursiv::Extends<Element> {
  vector<Base*> elements;
};
struct Alternatives : cursiv::Extends<Element> {
  vector<Base*> elements;
};

struct Grammar : cursiv::GrammarSpec {
  struct Expression;
  struct LeafRule : rule<assign<&Leaf::value>(lit<"a">)> {};
  struct Parent : rule<lit<"("> + ref<Expression> +
                       flag<&Element::parenthesized>(lit<")">)> {};
  struct Primary : rule<ref<LeafRule> | ref<Parent>> {};
  struct Token
      : rule<ref<Primary> + option(nest<&Counted::element>(
                                assign<&Counted::cardinality>(lit<"+">)))> {};
  struct Sequence
      : rule<create<Group>(append<&Group::elements>(ref<Token>) +
                           some(append<&Group::elements>(ref<Token>))) |
             ref<Token>> {};
  struct Expression : rule<create<Alternatives>(
                               append<&Alternatives::elements>(ref<Sequence>) +
                               some(lit<"|"> + append<&Alternatives::elements>(
                                                   ref<Sequence>))) |
                           ref<Sequence>> {};
  using entry = Expression;
};

}  // namespace tcyclicadopt

// A run nested in a run keeps the grouping it was written with, in both
// engines: the inner run takes another turn before stopping, so the items
// make one group unless the grammar forces a cut.

namespace tnestedrun {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {};
struct Group : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> xs;
};
struct Groups : cursiv::Extends<cursiv::AstNode> {
  vector<Group*> groups;
  vector<Item*> items;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ItemRule : rule<create<ast::Item>(lit<"x">)> {};
  using ignore = WS;
};

struct Plain : Base {
  struct Root
      : rule<many(some(append<&ast::Groups::items>(ref<ItemRule>)))> {};
  using entry = Root;
};

struct InPlace : Base {
  struct Root
      : rule<many(append<&ast::Groups::groups>(create<ast::Group>(
            some(append<&ast::Group::xs>(ref<ItemRule>)) +
            option(lit<";">))))> {};
  using entry = Root;
};

struct BehindRule : Base {
  struct GroupRule : rule<create<ast::Group>(
                         some(append<&ast::Group::xs>(ref<ItemRule>)) +
                         option(lit<";">))> {};
  struct Root : rule<many(append<&ast::Groups::groups>(ref<GroupRule>))> {};
  using entry = Root;
};

struct Suite : Base {
  // Plain remains nullable; the tagged host tests the empty rule as well.
  struct Root : rule<ref<Plain::Root> |
                     (lit<"inplace:"> + ref<InPlace::Root>) |
                     (lit<"behind:"> + ref<BehindRule::Root>)> {};
  using entry = Root;
};
}  // namespace tnestedrun

namespace ttree {

struct Combined : Base {
  struct Entry
      : rule<ref<Suite::Root> |
             (lit<"cyclic:"> +
              skip(ignored<>, hide<>,
                   ref<tcyclicadopt::Grammar::Expression> + eof)) |
             (lit<"run:"> + ref<tnestedrun::Suite::Root>) |
             (lit<"members:"> + ref<tmember::Forward::Root>) |
             section<QuickStart>(lit<"quick:">, lit<"@END@">) |
             (lit<"mat:"> + ref<tmaterialise::Combined::Entry>) |
             (lit<"dir:"> + ref<tdirseq::Combined::Entry>)> {};
  using entry = Entry;
};
struct Ll : Combined {};
struct Lr : Combined {};
// Only cyclic adoption needs forced auxiliary optionals. Keep that lowering
// independent without regenerating the unrelated tree/directive families.
struct Aux : Base {
  struct Entry
      : rule<lit<"cyclic:"> +
             skip(ignored<>, hide<>,
                  ref<tcyclicadopt::Grammar::Expression> + eof)> {};
  using entry = Entry;
};

}  // namespace ttree
