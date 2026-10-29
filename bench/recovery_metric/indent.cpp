// indent: suites opened by `:` and closed by a line back at an enclosing
// level, bracketed lists where a newline says nothing. The documents are
// generated here; the metric keeps only those the parser takes clean.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bench.hpp"
#include "cursiv/fuzz/CursivActions.hpp"
#include "engines/lr/IndentGrammar.hpp"
#include "indent/generated/G/AstReflection.hpp"
#include "indent/generated/G/Grammar.hpp"
#include "indent/generated/G/Module.hpp"
#include "indent/generated/G/Parser.hpp"
#include "indent/generated/G/Table.hpp"
#include "recovery_metric/Searching.hpp"
#include "recovery_metric/MetricLanguage.hpp"

namespace metric = cursiv::recovery_metric;

namespace {

namespace g = tindent::generated::G;
const cursiv::fuzzer::CursivActions<tindent::Plain> kActions{};

std::string word(std::uint64_t& s) {
  static constexpr char kLetters[] = "abcdefghijklmnopqrstuvwxyz";
  std::string w;
  const std::size_t n = 2 + bench::splitmix64(s) % 6;
  for (std::size_t i = 0; i < n; ++i)
    w += kLetters[bench::splitmix64(s) % 26];
  return w;
}

void block(std::string& out, std::uint64_t& s, int level, int depth) {
  const std::size_t n = 1 + bench::splitmix64(s) % 3;
  for (std::size_t i = 0; i < n; ++i) {
    out.append(static_cast<std::size_t>(level) * 2, ' ');
    out += word(s);
    const std::uint64_t pick = bench::splitmix64(s) % 5;
    if (pick < 2 && depth > 0) {
      out += ":\n";
      block(out, s, level + 1, depth - 1);
    } else if (pick == 2) {
      out += " [";
      const std::size_t items = 1 + bench::splitmix64(s) % 4;
      for (std::size_t k = 0; k < items; ++k) out += " " + word(s);
      out += " ]\n";
    } else {
      if (pick == 3) out += "  # " + word(s);
      out += "\n";
    }
  }
}

std::string document(std::uint64_t seed, int depth) {
  std::string out;
  for (int i = 0; i < 6; ++i) block(out, seed, 0, depth);
  return out;
}

const metric::Registered kLanguage{{
    .rank = 7,
    .name = "indent",
    .parser = &g::create_parser,
    .reflection = &g::create_ast_reflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{document(0x1d3e7, 3),
                                          document(0x1d3e8, 4),
                                          document(0x1d3e9, 5)};
        },
    .ceilings = &metric::ceilings<g::kTable, g::kDrive,
                                          g::kGrammarMetaData, g::kAstActions>,
    .searching = &metric::searching<g::kTable, g::kDrive,
                                                g::kGrammarMetaData,
                                                g::kAstActions>,
}};

}  // namespace
