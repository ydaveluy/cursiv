#pragma once

// Left recursion inside or behind optional items: each engine reads the
// alternatives the items spell, the item taken before the item left out.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlloption {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Node> {
  string_view name;
};
struct Path : cursiv::Extends<Node> {
  Node* head{};
  string_view name;
};
struct Array : cursiv::Extends<Node> {
  Node* inner{};
  bool constant{};
};
struct Marked : cursiv::Extends<Node> {
  Node* inner{};
  vector<string_view> marks;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Node* value{};
  string_view text;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<lit<"#"> + some(cr<"a-z">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NameRule : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {
  };

  // `(A ".")? ID`, the call inside the option.
  struct Dotted : rule<option(ref<Dotted> + lit<".">) + ref<ID>> {};
  struct PathRule
      : rule<create<ast::Path>(option(assign<&ast::Path::head>(ref<PathRule>) +
                                      lit<".">) +
                               assign<&ast::Path::name>(ref<ID>))> {};
  // ...and its alternatives written out.
  struct PathWritten
      : rule<create<ast::Path>(assign<&ast::Path::head>(ref<PathWritten>) +
                               lit<"."> + assign<&ast::Path::name>(ref<ID>)) |
             create<ast::Path>(assign<&ast::Path::name>(ref<ID>))> {};
  // `"p"? A "x" | "y"`, the call behind the option.
  struct Behind : rule<(option(lit<"p">) + ref<Behind> + lit<"x">) | lit<"y">> {
  };
  // `"const"? A "[]" | ID`, the option writing a flag.
  struct ArrayRule
      : rule<create<ast::Array>(
                 option(flag<&ast::Array::constant>(kw<"const">)) +
                 assign<&ast::Array::inner>(ref<ArrayRule>) + lit<"[]">) |
             ref<NameRule>> {};
  // A repetition in front of the call: `"@"* A "x" | ID`.
  struct MarkedRule
      : rule<create<ast::Marked>(many(append<&ast::Marked::marks>(lit<"@">)) +
                                 assign<&ast::Marked::inner>(ref<MarkedRule>) +
                                 lit<"x">) |
             ref<NameRule>> {};
  // Two options in front of the call: `"<"? ">"? A "!" | "z"`.
  struct Twice
      : rule<(option(lit<"<">) + option(lit<">">) + ref<Twice> + lit<"!">) |
             lit<"z">> {};
  // As many options as either engine spells out in front of the call.
  struct Seven
      : rule<(option(lit<"1">) + option(lit<"2">) + option(lit<"3">) +
              option(lit<"4">) + option(lit<"5">) + option(lit<"6">) +
              option(lit<"7">) + ref<Seven> + lit<"!">) |
             lit<"z">> {};
  // Eight options in all, one in front of the call: only that one counts.
  struct Late
      : rule<(option(lit<"1">) + ref<Late> + option(lit<"2">) +
              option(lit<"3">) + option(lit<"4">) + option(lit<"5">) +
              option(lit<"6">) + option(lit<"7">) + option(lit<"8">) +
              lit<"!">) |
             lit<"z">> {};
  // A counted repetition in front of the call.
  struct Counted
      : rule<(repeat<0, 2>(lit<"+">) + ref<Counted> + lit<"!">) | lit<"z">> {};
  // An option that is the whole of a `tight()` region, in front of the call:
  // left out, the region leaves no edge behind.
  struct Region
      : rule<(tight(option(lit<"<"> + lit<"-">)) + ref<Region> + lit<"!">) |
             lit<"z">> {};
  struct RegionArray
      : rule<create<ast::Array>(
                 tight(option(
                     flag<&ast::Array::constant>(lit<"<"> + lit<"<">))) +
                 assign<&ast::Array::inner>(ref<RegionArray>) + lit<"[]">) |
             ref<NameRule>> {};
  // The cycle through another rule, the option on its way back.
  struct Outer;
  struct Inner : rule<option(lit<"~">) + ref<Outer> + lit<";">> {};
  struct Outer : rule<ref<Inner> | lit<"o">> {};

  struct Root
      : rule<create<ast::Doc>(
                 (lit<"d:"> + assign<&ast::Doc::text>(ref<Dotted>)) |
                 (lit<"q:"> + assign<&ast::Doc::value>(ref<PathRule>)) |
                 (lit<"w:"> + assign<&ast::Doc::value>(ref<PathWritten>)) |
                 (lit<"b:"> + assign<&ast::Doc::text>(ref<Behind>)) |
                 (lit<"a:"> + assign<&ast::Doc::value>(ref<ArrayRule>)) |
                 (lit<"m:"> + assign<&ast::Doc::value>(ref<MarkedRule>)) |
                 (lit<"t:"> + assign<&ast::Doc::text>(ref<Twice>)) |
                 (lit<"s:"> + assign<&ast::Doc::text>(ref<Seven>)) |
                 (lit<"r:"> + assign<&ast::Doc::text>(ref<Region>)) |
                 (lit<"g:"> + assign<&ast::Doc::value>(ref<RegionArray>)) |
                 (lit<"e:"> + assign<&ast::Doc::text>(ref<Late>)) |
                 (lit<"c:"> + assign<&ast::Doc::text>(ref<Counted>)) |
                 (lit<"o:"> + assign<&ast::Doc::text>(ref<Outer>))) +
             eof> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tlloption
