#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/utils/CaseFold.hpp"

namespace cursiv::engine {

struct FoldedWord {
  std::string_view text;
  // A plain spelling, the root word boundary, a pending guard, or a matcher entry.
  std::uint32_t guard;
  static constexpr std::uint32_t kPlain = ~std::uint32_t{0};
  static constexpr std::uint32_t kWord = kPlain - 1;
  static constexpr std::uint32_t kNever = kPlain - 2;
  static constexpr std::uint32_t kMatcher = 1u << 31;
};

struct GrammarWord {
  std::string_view text;
  bool folded = false;
  std::string_view doc{};

  // The same key on the generator host and on the parser's target.
  static std::uint64_t key(std::string_view word) noexcept {
    return detail::folded_scalars_hash(word.data(), word.size());
  }

  // A null view has no prose yet. An empty, non-null view records a conflict.
  static std::string_view shared_doc(std::string_view a, std::string_view b) noexcept {
    if (a.data() == nullptr) return b;
    if (b.data() == nullptr || a == b) return a;
    return "";
  }

  void merge(const GrammarWord& other) {
    folded |= other.folded;
    doc = shared_doc(doc, other.doc);
  }
};

}  // namespace cursiv::engine
