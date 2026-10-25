#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tunorderedturns {

struct Tail : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Wrap : cursiv::Extends<Expr> {
  Expr* inner{};
  string_view op;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Tail*> tails;
  vector<string_view> labels;
  Expr* expression{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  // `+1` is one LABEL or `+` then a LABEL: a loop over `+ LABEL` and the
  // LABEL after it rival over the same bytes.
  struct LABEL : terminal<some(cr<"a-zA-Z0-9+">)> {};
  struct AddTail
      : rule<lit<"+"> + option(ref<WS>) + assign<&Tail::name>(ref<LABEL>)> {};
  struct Postfix : rule<create<Name>(assign<&Name::name>(ref<LABEL>)) +
                        (nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">)) &
                         nest<&Wrap::inner>(assign<&Wrap::op>(lit<"?">)) &
                         nest<&Wrap::inner>(assign<&Wrap::op>(lit<"~">)))> {};
  struct Root
      : rule<create<Doc>(
                 (lit<"!"> + assign<&Doc::name>(ref<LABEL>) +
                  many(option(ref<WS>) + append<&Doc::tails>(ref<AddTail>)) +
                  ref<WS> + append<&Doc::labels>(ref<LABEL>) +
                  many(ref<WS> + append<&Doc::labels>(ref<LABEL>)) + lit<";">) |
                 (lit<"?"> +
                  ((assign<&Doc::name>(ref<LABEL>) +
                    many(option(ref<WS>) + append<&Doc::tails>(ref<AddTail>)) +
                    ref<WS> + append<&Doc::labels>(ref<LABEL>) +
                    many(ref<WS> + append<&Doc::labels>(ref<LABEL>))) &
                   lit<";">)) |
                 (lit<"*"> + assign<&Doc::expression>(ref<Postfix>))) +
             eof> {};
  using entry = Root;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tunorderedturns
