// The grammar's own configuration and a region for each of 255 terminals: a
// position names the configuration it is read under by one byte.
// expect LR LL: refused: the grammar reads under more than 255 skip configurations
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/typelist.hpp"

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
  // Whitespace keeps the grammar's own configuration distinct from every
  // subset.
  using ignored = WS;
};

}  // namespace refuse
