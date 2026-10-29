#pragma once

// Grammars wider than a fixed index width: the construction holds them, and
// the frozen table's own widths are the only limits.

#include <cstddef>
#include <utility>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/LongRunGrammar.hpp"

namespace tbounds {

// Callers and rules by the hundred: every caller's state opens the whole
// choice, past 65 535 nonterminal transitions in all.
namespace transitions {
struct Parts : cursiv::GrammarSpec {
  template <std::size_t I>
  struct X : rule<lit<"x">> {};
  template <std::size_t I>
  struct A : rule<lit<"a">> {};
};
template <std::size_t... Is>
constexpr auto choice(std::index_sequence<Is...>) {
  return (cursiv::ref<Parts::A<Is>> | ...);
}
template <class Stmt, std::size_t... Is>
constexpr auto callers(std::index_sequence<Is...>) {
  return ((cursiv::ref<Parts::X<Is>> + cursiv::ref<Stmt>) | ...);
}
struct G : Parts {
  struct WS : terminal<some(s)> {};
  struct Stmt : rule<choice(std::make_index_sequence<256>{})> {};
  struct Item : rule<callers<Stmt>(std::make_index_sequence<256>{})> {};
  struct S : rule<many(ref<Item>)> {};
  using entry = S;
  using ignored = WS;
};
}  // namespace transitions

// A cell the second token decides in every context: after `xaa c q`, `z` or
// `w` says whether `c` was an `A` or a `B`. Each cell opens on a word of its
// own, or the cells would read alike and only the first would stay.
namespace peeks {
template <std::size_t I>
constexpr cursiv::str<4> opening() {
  const char s[4] = {'x', static_cast<char>('a' + I / 26),
                     static_cast<char>('a' + I % 26), 0};
  return cursiv::str<4>(s);
}
struct Parts : cursiv::GrammarSpec {
  template <std::size_t I>
  struct X : rule<lit<opening<I>()>> {};
  template <std::size_t I>
  struct A : rule<lit<"c">> {};
  template <std::size_t I>
  struct B : rule<lit<"c">> {};
};
template <std::size_t... Is>
constexpr auto cells(std::index_sequence<Is...>) {
  return ((cursiv::ref<Parts::X<Is>> +
           ((cursiv::ref<Parts::A<Is>> + cursiv::lit<"q"> + cursiv::lit<"z">) |
            (cursiv::ref<Parts::B<Is>> + cursiv::lit<"q"> +
             cursiv::lit<"w">))) |
          ...);
}
struct G : Parts {
  struct WS : terminal<some(s)> {};
  struct S : rule<cells(std::make_index_sequence<32>{})> {};
  using entry = S;
  using ignored = WS;
};
}  // namespace peeks

// Seventy keywords sharing their first byte: none is eligible for a direct
// one-byte probe or an exclusive named-terminal entry.
namespace probes {
namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast
template <std::size_t I>
constexpr cursiv::str<4> word() {
  const char s[4] = {'k', static_cast<char>('a' + I / 26),
                     static_cast<char>('a' + I % 26), 0};
  return cursiv::str<4>(s);
}
template <std::size_t... Is>
constexpr auto words(std::index_sequence<Is...>) {
  return (cursiv::kw<word<Is>()> | ...);
}
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct S : rule<create<ast::Doc>(words(std::make_index_sequence<70>{}))> {};
  using entry = S;
  using ignored = WS;
};
}  // namespace probes

// Distinct one-byte literals exercise the entry probe's actual capacity, in
// the state a shift enters: only there does a probe run.
namespace byteprobes {
template <std::size_t I>
constexpr cursiv::str<2> byte() {
  const char s[2] = {static_cast<char>('!' + I), 0};
  return cursiv::str<2>(s);
}
template <std::size_t... Is>
constexpr auto bytes(std::index_sequence<Is...>) {
  return (cursiv::lit<byte<Is>()> | ...);
}
template <std::size_t Count>
struct G : cursiv::GrammarSpec {
  struct S : rule<create<probes::ast::Doc>(
                 lit<"~"> + bytes(std::make_index_sequence<Count>{}))> {};
  using entry = S;
};
}  // namespace byteprobes

}  // namespace tbounds
