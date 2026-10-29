// 16 distinct conditions of the gap guard 16 alternatives that all shift one
// token: a state stands for each subset of them.
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
  return ((cursiv::gap::has(cursiv::lit<guard_word<Is>()>) + cursiv::lit<"x">) |
          ...);
}

struct G : Base {
  struct Root : rule<create<ast::Node>(
                    many(guarded_shifts(std::make_index_sequence<16>{})))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
