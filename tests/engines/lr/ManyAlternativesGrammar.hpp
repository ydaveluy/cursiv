#pragma once

// A hundred and fifty alternatives read `a t`, and one reads `a u` in the
// middle of them: after `a`, `t` is written first by the alternatives before
// the `u` one, and `u` has the lesser id.

#include <cstddef>
#include <utility>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tmany {

struct Item : cursiv::AstNode {};
struct Doc : cursiv::AstNode {
  Item* item = nullptr;
};

template <std::size_t I>
constexpr cursiv::str<4> word() {
  const char s[4] = {'w', static_cast<char>('a' + I / 26),
                     static_cast<char>('a' + I % 26), 0};
  return cursiv::str<4>(s);
}

struct Parts : cursiv::GrammarSpec {
  template <std::size_t I>
  struct A : rule<create<Item>(kw<"a"> + kw<"t"> + kw<word<I>()>)> {};
  struct B : rule<create<Item>(kw<"a"> + kw<"u"> + kw<"z">)> {};
  struct Early : rule<create<Item>(kw<"u"> + kw<"q">)> {};
};

// `Early`, the first `Before` alternatives, `B`, and `After` more.
template <std::size_t... Bs, std::size_t... As>
constexpr auto pick(std::index_sequence<Bs...>, std::index_sequence<As...>) {
  const auto head =
      (cursiv::ref<Parts::Early> | ... | cursiv::ref<Parts::A<Bs>>);
  const auto middle = head | cursiv::ref<Parts::B>;
  return (middle | ... | cursiv::ref<Parts::A<sizeof...(Bs) + As>>);
}

struct G : Parts {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct Pick : rule<pick(std::make_index_sequence<130>{},
                          std::make_index_sequence<20>{})> {};
  struct Root : rule<create<Doc>(assign<&Doc::item>(ref<Pick>)) + eof> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tmany
