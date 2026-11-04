#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace cursiv {

// Immutable generated source and direct dependency ranks. All views outlive
// the service; no rule identity is recovered by parsing its display text.
struct GrammarSourceRule {
  std::string_view name;
  std::string_view text;
  std::span<const std::size_t> dependencies;
};

struct GrammarSourceData {
  std::string_view text;
  std::span<const GrammarSourceRule> rules;
};

}  // namespace cursiv
