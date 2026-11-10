// The grammar's own trivia setting plus 255 `skip()` regions: 256 settings,
// one more than the limit.
// expect LR LL: refused: the grammar uses more than 255 trivia settings
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/typelist.hpp"

namespace refuse {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Parts : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  template <std::size_t I>
  struct Token : terminal<repeat<I + 1>(lit<"#">)> {};
};

template <std::size_t Mask, std::size_t... Bits>
constexpr auto region(std::index_sequence<Bits...>) {
  using Tokens = cursiv::detail::tl_cat_t<std::conditional_t<
      (Mask & (1u << Bits)) != 0, cursiv::TypeList<Parts::Token<Bits>>,
      cursiv::TypeList<>>...>;
  return []<class... Ts>(cursiv::TypeList<Ts...>) {
    return cursiv::skip(cursiv::ignore<Ts...>, cursiv::hide<>,
                        cursiv::lit<"x"> + cursiv::lit<"y">);
  }(Tokens{});
}

template <std::size_t... Is>
constexpr auto regions(std::index_sequence<Is...>) {
  // A flat sequence avoids normalizing every growing prefix.
  return cursiv::Seq<cursiv::Lit<"begin">,
                     decltype(region<Is + 1>(
                         std::make_index_sequence<8>{}))...>{};
}

struct G : Parts {
  struct Root : rule<create<Doc>(regions(std::make_index_sequence<255>{}))> {};
  using entry = Root;
  // Ignoring `WS` keeps the grammar's own setting distinct from every region's.
  using ignored = WS;
};

}  // namespace refuse
