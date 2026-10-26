// Eighty regions, each skipping a terminal of its own: the LL parser must
// preserve the selected skip configuration and the expected words.

#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/SkipConfigsGrammar.hpp"

#include "semanticll/generated/Ll/Module.hpp"

namespace ll_skip_configs_cases {

namespace {

constexpr unsigned kRegions = 80;

std::string text(unsigned skipped, unsigned inside) {
  std::string out = "begin";
  for (unsigned i = 0; i < kRegions; ++i) {
    const auto n = std::to_string(i);
    out += " x" + n;
    if (i == skipped) out += "#" + std::to_string(inside);
    out += "y" + n;
  }
  return out;
}

std::string words(const cursiv::ParseResult& r) {
  const auto* doc = r.value_as<tskipcfg::Doc>();
  std::string out;
  if (doc != nullptr)
    for (const std::string_view w : doc->words) out += std::string(w) + ",";
  return out;
}

void read(const cursiv::Parser& ll, const std::string& source, bool expected) {
  const std::string input = "skip:" + source;
  const auto a = ll.parse(input);
  const bool clean = a.accepted && a.diagnostics.empty();
  CHECK(clean == expected);
  if (expected) {
    std::string every;
    for (unsigned i = 0; i < kRegions; ++i)
      every += "x" + std::to_string(i) + ",y" + std::to_string(i) + ",";
    CHECK(words(a) == every);
  }
}

}  // namespace

void run() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  read(*ll, text(kRegions, 0), true);
  for (const unsigned i : {0u, 1u, 31u, 32u, 63u, 64u, 65u, 79u}) {
    read(*ll, text(i, i), true);
    read(*ll, text(i, (i + 1) % kRegions), false);
  }
}

}  // namespace ll_skip_configs_cases
