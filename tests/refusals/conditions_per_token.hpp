// 17 distinct gap conditions guard 17 alternatives that all read `x`: the
// parser needs a state for each subset of them.
// expect LR: refused: the grammar is too large for the generated parser: more than 16 conditions apply to one token
#pragma once

#include <cstddef>
#include <utility>

#include "refusals/head.hpp"

namespace refuse {

template <std::size_t I>
constexpr cursiv::str<3> guard_word() {
  const char s[3] = {'c', static_cast<char>('a' + I), 0};
  return cursiv::str<3>(s);
}
template <std::size_t... Is>
constexpr auto guarded_shifts(std::index_sequence<Is...>) {
  return ((cursiv::GrammarSpec::gap::has(
               cursiv::GrammarSpec::lit<guard_word<Is>()>) +
           cursiv::GrammarSpec::lit<"x">) |
          ...);
}

struct G : Base {
  struct Root : rule<create<ast::Node>(
                    many(guarded_shifts(std::make_index_sequence<17>{})))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
