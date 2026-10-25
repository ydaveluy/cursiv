// recover_bench -- the repair path under measurement: one document, altered at
// many sites, parsed to a tree. Prints a fingerprint of every tree so a change
// that claims to be behaviour-preserving is checked, not asserted.
//
//   recover_bench <lang> <bytes> <sites> <reps> [--print]
//   lang: arithmetics (the default) or domainmodel

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "arithmetics/generated/Arith/Module.hpp"
#include "bench.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"

namespace {

std::uint64_t fingerprint(const cursiv::Cst& tree, bool accepted) {
  std::uint64_t h = 1469598103934665603ull;
  const auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
  mix(accepted);
  mix(tree.size());
  for (cursiv::NodeId id = 0; id < tree.size(); ++id) {
    const auto& n = tree[id];
    mix(n.begin); mix(n.end); mix(n.subtreeSize);
    mix(n.hidden); mix(n.recovered); mix(n.engine);
  }
  mix(tree.diagnostics().size());
  for (const auto& d : tree.diagnostics()) {
    mix(static_cast<unsigned>(d.kind)); mix(d.element);
    mix(d.begin); mix(d.end);
    for (auto v : d.also) mix(v);
  }
  return h;
}

// An expression language and a language of nested declarations, each with
// the misspelling of one of its own keywords.
struct Language {
  std::string_view name;
  std::unique_ptr<cursiv::Parser> (*parser)();
  std::string (*generate)(std::size_t);
  std::string_view misspelled;
};

constexpr Language kLanguages[] = {
    {"arithmetics", &arith::generated::Arith::create_parser,
     &bench::generateArithmeticsSource, "deef"},
    {"domainmodel", &dm::generated::DomainModel::create_parser,
     &bench::generateDomainModelSource, "enntity"},
};

// Three alterations per site: a byte dropped, a burst of punctuation wedged
// in, and a keyword-shaped misspelling -- the three the repair search answers
// with a skip, an insert and a rescan.
std::vector<std::string> alterations(const std::string& text,
                                     std::size_t sites,
                                     std::string_view misspelled) {
  std::vector<std::string> out;
  out.push_back(text);
  for (std::size_t i = 0; i < sites; ++i) {
    const std::size_t at = text.size() * (i + 1) / (sites + 1);
    std::string a = text; a.erase(at, 1); out.push_back(std::move(a));
    std::string b = text; b.insert(at, "@ );"); out.push_back(std::move(b));
    std::string c = text; c.insert(at, misspelled); out.push_back(std::move(c));
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view lang = argc > 1 ? argv[1] : "arithmetics";
  const std::size_t bytes = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 65536;
  const std::size_t sites = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 64;
  const int reps = argc > 4 ? std::atoi(argv[4]) : 5;
  const bool print = argc > 5 && std::string_view(argv[5]) == "--print";

  const Language* language = nullptr;
  for (const Language& l : kLanguages)
    if (l.name == lang) language = &l;
  if (language == nullptr) {
    std::fprintf(stderr, "recover_bench: no language '%.*s'\n",
                 static_cast<int>(lang.size()), lang.data());
    return 2;
  }
  const std::unique_ptr<cursiv::Parser> engine = language->parser();
  const std::string text = language->generate(bytes);
  const std::vector<std::string> docs =
      alterations(text, sites, language->misspelled);

  std::uint64_t sink = 0;
  double best = 1e9;
  for (int r = 0; r < reps; ++r) {
    std::uint64_t h = 0;
    const double s = bench::seconds_of([&] {
      for (const std::string& d : docs) {
        cursiv::ParseResult res = engine->parse(d);
        h ^= fingerprint(res.cst(), res.cst().diagnostics().empty());
      }
    });
    sink ^= h;
    if (s < best) best = s;
  }
  if (print)
    for (const std::string& d : docs) {
      cursiv::ParseResult res = engine->parse(d);
      std::printf("%zu %016llx\n", d.size(),
                  static_cast<unsigned long long>(
                      fingerprint(res.cst(), res.cst().diagnostics().empty())));
    }
  std::printf("%.*s %zu docs %zu bytes best %.6f s sink %016llx\n",
              static_cast<int>(lang.size()), lang.data(), docs.size(),
              text.size(), best, static_cast<unsigned long long>(sink));
  return 0;
}
