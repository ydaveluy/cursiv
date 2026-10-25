#pragma once

// Grammars written the natural way, each with a text that reads many ways: the
// readings pack in one graph, so a parse costs a power of the text's length and
// never the number of ways it reads.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// A run of lines that is a section of its own, inside a document that is a
// run of sections: N lines group in 2^(N-1) ways.
namespace tgfasta {
namespace ast {
struct Section : cursiv::Extends<cursiv::AstNode> {};
struct Description : cursiv::Extends<Section> {
  string_view text;
};
struct Line : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Run : cursiv::Extends<Section> {
  vector<Line*> lines;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Section*> sections;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  static constexpr auto line_end = option(lit<"\r">) + lit<"\n">;
  struct TEXT : terminal<some(cr<"0-9A-Za-z ">)> {};
  struct SEQ : terminal<some(cr<"A-Za-z">)> {};
  struct DescriptionRule
      : rule<lit<">"> + assign<&ast::Description::text>(ref<TEXT>) + line_end> {
  };
  struct LineRule : rule<assign<&ast::Line::text>(ref<SEQ>) + line_end> {};
  struct RunRule : rule<some(append<&ast::Run::lines>(ref<LineRule>))> {};
  struct SectionRule : rule<ref<DescriptionRule> | ref<RunRule>> {};
  struct DocumentRule
      : rule<some(append<&ast::Document::sections>(ref<SectionRule>))> {};
  using entry = DocumentRule;
};
struct Lr : Base {};
}  // namespace tgfasta

// An element whose content and close are optional as one unit: which applies
// depends on a close tag arbitrarily far ahead.
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
  using ignore = WS;
};
struct Lr : Base {};
}  // namespace tghtml

// A call's arguments with no delimiter, an argument itself a call or a name:
// a run of names is one call's arguments or calls of its own.
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
  using ignore = WS;
};
struct Lr : Base {};
}  // namespace tgcall

// A chain written twice: as a left-recursive operand and as a loop over the
// same suffixes, so each suffix is the operand's or the loop's.
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
  using ignore = WS;
};
struct Lr : Base {};
}  // namespace tgchain

// The three expression/tree growth classes share one whitespace lexicon.
namespace tgrowth {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<(kw<"html"> + ref<tghtml::Base::ElemRule>) |
                     (kw<"call"> + ref<tgcall::Base::ProgramRule>) |
                     (kw<"chain"> + ref<tgchain::Base::Entry>)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace tgrowth
