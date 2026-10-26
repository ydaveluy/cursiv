#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tunorderedturns {

struct Tail : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Tail*> tails;
  vector<string_view> labels;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  // `+1` is one LABEL or `+` then a LABEL: a loop over `+ LABEL` and the
  // LABEL after it rival over the same bytes.
  struct LABEL : terminal<some(cr<"a-zA-Z0-9+">)> {};
  struct AddTail
      : rule<lit<"+"> + option(ref<WS>) + assign<&Tail::name>(ref<LABEL>)> {};
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
                   lit<";">))) +
             eof> {};
  using entry = Root;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tunorderedturns
