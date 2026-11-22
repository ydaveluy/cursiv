#pragma once

// Three texts the LR graph stack reads many ways: an element whose content and
// close are optional as one unit, calls whose arguments have no delimiter, and
// a chain written both as an operand and as a loop.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

// Which of the element's two readings applies depends on a close tag
// arbitrarily far ahead.
namespace tghtml {
namespace ast {
struct Elem : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<cursiv::AstNode*> content;
  string_view closeName;
};
struct Text : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct OPEN : terminal<lit<"<">> {};
  struct SLASH : terminal<lit<"/">> {};
  struct CLOSE : terminal<lit<">">> {};
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct WORD : terminal<some(cr<"A-Z0-9">)> {};
  struct ElemRule;
  struct TextRule : rule<assign<&ast::Text::text>(ref<WORD>)> {};
  static constexpr auto open_prefix =
      ref<OPEN> + assign<&ast::Elem::name>(ref<NAME>);
  static constexpr auto close_tag = ref<OPEN> + ref<SLASH> +
                                    assign<&ast::Elem::closeName>(ref<NAME>) +
                                    ref<CLOSE>;
  struct ElemRule : rule<open_prefix + ref<CLOSE> +
                         option(many(append<&ast::Elem::content>(
                                    ref<ElemRule> | ref<TextRule>)) +
                                close_tag)> {};
  using entry = ElemRule;
  using ignored = WS;
};
}  // namespace tghtml

// A run of names is one call's arguments or calls of their own.
namespace tgcall {
namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Var : cursiv::Extends<Node> {
  string_view name;
};
struct Call : cursiv::Extends<Node> {
  string_view callee;
  vector<Node*> args;
};
struct Program : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> statements;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<some(cr<"a-z">)> {};
  struct CallRule;
  struct VarRule : rule<assign<&ast::Var::name>(ref<NAME>)> {};
  struct ExprRule : rule<ref<CallRule> | ref<VarRule>> {};
  struct CallRule
      : rule<assign<&ast::Call::callee>(ref<NAME>) +
             some(append<&ast::Call::args>(ref<ExprRule>)) + lit<"!">> {};
  struct ProgramRule
      : rule<some(append<&ast::Program::statements>(ref<ExprRule>))> {};
  using entry = ProgramRule;
  using ignored = WS;
};
}  // namespace tgcall

// Each suffix is the operand's or the loop's.
namespace tgchain {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Basic;
  struct Basic : rule<ref<ID> | (ref<Basic> + lit<"."> + ref<ID>)> {};
  struct Postfix : rule<ref<Basic> + many(lit<"."> + ref<ID>)> {};
  struct Entry : rule<create<Node>(ref<Postfix>)> {};
  using entry = Entry;
  using ignored = WS;
};
}  // namespace tgchain

namespace tgrowth {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<(kw<"html"> + ref<tghtml::Base::ElemRule>) |
                     (kw<"call"> + ref<tgcall::Base::ProgramRule>) |
                     (kw<"chain"> + ref<tgchain::Base::Entry>)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tgrowth
