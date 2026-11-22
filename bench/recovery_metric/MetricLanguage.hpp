// A language the recovery metric runs: its parser, how its tree is read and its
// valid documents. Each language registers itself, so a binary runs what it
// links.
#pragma once

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/GrammarActions.hpp"

namespace cursiv::recovery_metric {

struct MetricLanguage {
  // The report's order.
  int rank = 0;
  std::string_view name;
  std::unique_ptr<cursiv::Parser> (*parser)() = nullptr;
  std::unique_ptr<cursiv::AstReflection> (*reflection)() = nullptr;
  const cursiv::fuzzer::GrammarActions* actions = nullptr;
  std::vector<std::string> (*documents)() = nullptr;
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

}  // namespace cursiv::recovery_metric
