// asi: statements a semicolon, a line break, a closing brace or the end of
// input ends. The documents are generated here; the metric keeps only those
// the parser takes clean. The fuzz lane's directive reader models none of
// its conditions, so its tree is weighed on classes alone.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "asi/generated/G/AstReflection.hpp"
#include "asi/generated/G/Grammar.hpp"
#include "asi/generated/G/Module.hpp"
#include "asi/generated/G/Parser.hpp"
#include "asi/generated/G/Table.hpp"
#include "bench.hpp"
#include "recovery_metric/Searching.hpp"
#include "recovery_metric/MetricLanguage.hpp"

namespace metric = cursiv::recovery_metric;

namespace {

namespace g = tasi::generated::G;
std::string word(std::uint64_t& s) {
  static constexpr char kLetters[] = "abcdefhijklmnopqstuvwxyz";
  std::string w;
  const std::size_t n = 3 + bench::splitmix64(s) % 5;
  for (std::size_t i = 0; i < n; ++i)
    w += kLetters[bench::splitmix64(s) % (sizeof kLetters - 1)];
  return w;
}

std::string expr(std::uint64_t& s, int depth) {
  std::string e = word(s);
  if (depth > 0 && bench::splitmix64(s) % 2 == 0) {
    e += "(";
    const std::size_t n = bench::splitmix64(s) % 3;
    for (std::size_t i = 0; i < n; ++i)
      e += (i == 0 ? "" : ", ") + expr(s, depth - 1);
    e += ")";
  }
  return e;
}

void statements(std::string& out, std::uint64_t& s, int level, int depth) {
  const std::size_t n = 2 + bench::splitmix64(s) % 3;
  for (std::size_t i = 0; i < n; ++i) {
    out.append(static_cast<std::size_t>(level) * 2, ' ');
    const std::uint64_t pick = bench::splitmix64(s) % 6;
    if (pick == 0 && depth > 0) {
      out += "{\n";
      statements(out, s, level + 1, depth - 1);
      out.append(static_cast<std::size_t>(level) * 2, ' ');
      out += "}\n";
      continue;
    }
    if (pick == 1)
      out += "return " + expr(s, 2);
    else if (pick == 2)
      out += "go " + expr(s, 1);
    else
      out += expr(s, 3);
    out += bench::splitmix64(s) % 2 == 0 ? ";\n" : "\n";
  }
}

std::string document(std::uint64_t seed, int depth) {
  std::string out;
  for (int i = 0; i < 4; ++i) statements(out, seed, 0, depth);
  return out;
}

const metric::Registered kLanguage{{
    .rank = 8,
    .name = "asi",
    .parser = &g::create_parser,
    .reflection = &g::create_ast_reflection,
    .actions = nullptr,
    .documents =
        [] {
          return std::vector<std::string>{document(0xa51, 2),
                                          document(0xa52, 3),
                                          document(0xa53, 4)};
        },
    .ceilings = &metric::ceilings<g::kTable, g::kDrive,
                                          g::kGrammarMetaData, g::kAstActions>,
    .searching = &metric::searching<g::kTable, g::kDrive,
                                                g::kGrammarMetaData,
                                                g::kAstActions>,
}};

}  // namespace
