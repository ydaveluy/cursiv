// A language the recovery metric runs: its erased parser, the readers its
// tree is weighed with, the valid documents faults are planted in, and --
// where the table engine generated it -- the ceilings one parse reached.
// Each language's unit registers itself, so a binary runs what it links.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/fuzz/GrammarActions.hpp"

namespace cursiv {
struct RepairBudget;
}

namespace recovery_metric {

inline constexpr std::array<std::string_view, 8> kCeilings = {
    "search", "look", "bounded", "reach", "budget", "degraded", "replan",
    "episode"};
using Ceilings = std::array<std::uint32_t, kCeilings.size()>;

struct MetricLanguage {
  // The report's order.
  int rank = 0;
  std::string_view name;
  std::unique_ptr<cursiv::Parser> (*parser)() = nullptr;
  std::unique_ptr<cursiv::AstReflection> (*reflection)() = nullptr;
  const cursiv::fuzzer::GrammarActions* actions = nullptr;
  std::vector<std::string> (*documents)() = nullptr;
  // Null for a parser that counts none; under the budget `searching`'s
  // parser repairs with.
  Ceilings (*ceilings)(std::string_view,
                       const cursiv::RepairBudget& budget) = nullptr;
  // The same language under a budget; null where its engine takes none.
  std::unique_ptr<cursiv::Parser> (*searching)(const cursiv::RepairBudget&) =
      nullptr;
};

inline std::vector<MetricLanguage>& languages() {
  static std::vector<MetricLanguage> all;
  return all;
}

struct Registered {
  explicit Registered(const MetricLanguage& l) { languages().push_back(l); }
};

// A document of the source tree; empty when it is not there.
inline std::string source_file(std::string_view path) {
  std::ifstream f(std::string(CURSIV_SOURCE_DIR) + "/" + std::string(path),
                  std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace recovery_metric
