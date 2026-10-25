#pragma once

// A document of more positions than fourteen bits of a record's state number:
// the letters the caret reads past position 16384 are still the grammar's. The
// letters stand in chunks, since a sequence is read one element per template
// depth.

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace twidepos {

namespace ast {
struct Document : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

inline constexpr std::size_t kChunk = 250;
inline constexpr std::size_t kChunks = 66;
inline constexpr std::size_t kLeading = kChunk * kChunks;

template <std::size_t, class T>
using Repeated = T;

using A = std::remove_cvref_t<decltype(cursiv::lit<"a">)>;
using B = std::remove_cvref_t<decltype(cursiv::lit<"b">)>;
using C = std::remove_cvref_t<decltype(cursiv::lit<"c">)>;

template <std::size_t... Is>
constexpr auto letters(std::index_sequence<Is...>) {
  return cursiv::Seq<Repeated<Is, A>...>{};
}

template <template <std::size_t> class Chunk, std::size_t... Is>
constexpr auto chunks(std::index_sequence<Is...>) {
  return cursiv::Seq<cursiv::Ref<Chunk<Is>>..., B, C>{};
}

struct Base : cursiv::GrammarSpec {
  template <std::size_t K>
  struct Chunk : rule<letters(std::make_index_sequence<kChunk>{})> {};
  struct Root : rule<create<ast::Document>(
                    chunks<Chunk>(std::make_index_sequence<kChunks>{}))> {};
  using entry = Root;
};
struct Ll : Base {};

}  // namespace twidepos
