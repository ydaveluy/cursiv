#pragma once

#include <cstddef>
#include <utility>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// One production past what a byte counts: both its items' dot and the
// insertions its first state owes run beyond 255.
namespace tbounds::longrun {
namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast
template <std::size_t... Is>
constexpr auto run(std::index_sequence<Is...>) {
  return ((void(Is), cursiv::lit<"a">) + ...);
}
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct S : rule<create<ast::Doc>(run(std::make_index_sequence<256>{}))> {};
  using entry = S;
  using ignored = WS;
};
}  // namespace tbounds::longrun
