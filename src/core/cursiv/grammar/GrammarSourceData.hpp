#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace cursiv::detail {

// Generated source text and direct dependency ranks; all views outlive the
// service.
struct GrammarSourceRule {
  std::string_view name;
  std::string_view text;
  std::span<const std::size_t> dependencies;
};

struct GrammarSourceData {
  std::string_view text;
  std::span<const GrammarSourceRule> rules;
};

}  // namespace cursiv::detail
