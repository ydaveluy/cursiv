// recover_bench -- the repair path: one document, altered at many sites,
// repaired to a tree, with a fingerprint of each tree printed.
//
//   recover_bench <language> <bytes> <sites> <reps> [--faults <k>]
//                 [--only <drop|burst|typo|unclosed>] [--print]
//
// Each altered copy carries k faults of one alteration, one by default. With
// no site the document is parsed alone. growth_bench is the same lane over
// the growth grammar of the suites.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bench.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#ifdef GROWTH_BENCH
#include "growth/generated/G/Module.hpp"
#else
#include "arithmetics/generated/Arith/Module.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#endif

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

// A language, the misspelling of one of its own keywords and what opens its
// block comment, set apart from the text around it.
struct Language {
  std::string_view name;
  std::unique_ptr<cursiv::Parser> (*parser)();
  std::string (*generate)(std::size_t);
  std::string_view misspelled;
  std::string_view commentOpener = {};
};

#ifdef GROWTH_BENCH
// About `bytes` of `unit` between `head` and `tail`.
std::string repeated(std::string head, std::string_view unit,
                     std::size_t bytes, std::string_view tail = {}) {
  while (head.size() + tail.size() < bytes) head += unit;
  return head += tail;
}

// Three texts the LR graph stack reads many ways: a chain that is an operand
// or a loop, arguments with no delimiter, elements whose close is optional.
constexpr Language kLanguages[] = {
    {"chain", &tgrowth::generated::G::create_parser,
     [](std::size_t bytes) { return repeated("chain a", ".b", bytes); },
     "chaain"},
    {"call", &tgrowth::generated::G::create_parser,
     [](std::size_t bytes) { return repeated("call f ", "a ", bytes, "!"); },
     "caall"},
    {"html", &tgrowth::generated::G::create_parser,
     [](std::size_t bytes) {
       return repeated("html <div>", "<p><br>X</p>", bytes, "</div>");
     },
     "htmml"},
};
#else
// An expression language and a language of nested declarations.
constexpr Language kLanguages[] = {
    {"arithmetics", &arith::generated::Arith::create_parser,
     &bench::generateArithmeticsSource, "deef", " /* "},
    {"domainmodel", &dm::generated::DomainModel::create_parser,
     &bench::generateDomainModelSource, "enntity", " /* "},
};
#endif

// What one fault does at its site: bytes dropped, then text wedged in.
struct Alteration {
  std::string_view name;
  std::size_t dropped;
  std::string_view wedged;
};

// A byte dropped, a burst of punctuation and a keyword-shaped misspelling,
// which the repair search answers with a skip, an insert and a rescan. A block
// comment opened and never closed runs only when named.
std::vector<Alteration> alterations_of(const Language& language,
                                       std::string_view only) {
  std::vector<Alteration> all = {
      {"drop", 1, {}}, {"burst", 0, "@ );"}, {"typo", 0, language.misspelled}};
  if (only.empty()) return all;
  if (!language.commentOpener.empty())
    all.push_back({"unclosed", 0, language.commentOpener});
  std::erase_if(all, [&](const Alteration& a) { return a.name != only; });
  return all;
}

// The text, then one copy per site and alteration with `faults` faults: each
// sits in its share of the text where the site sits in the whole.
std::vector<std::string> altered(const std::string& text, std::size_t sites,
                                 std::size_t faults,
                                 std::span<const Alteration> alterations) {
  std::vector<std::string> out;
  out.push_back(text);
  for (std::size_t i = 0; i < sites; ++i)
    for (const Alteration& a : alterations) {
      std::string d = text;
      for (std::size_t f = faults; f-- > 0;)
        d.replace(text.size() * (f * (sites + 1) + i + 1) /
                      (faults * (sites + 1)),
                  a.dropped, a.wedged);
      out.push_back(std::move(d));
    }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view lang = argc > 1 ? argv[1] : kLanguages[0].name;
  const std::size_t bytes = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 65536;
  const std::size_t sites = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 64;
  const int reps = argc > 4 ? std::atoi(argv[4]) : 5;
  std::size_t faults = 1;
  std::string_view only;
  bool print = false;
  bool refused = false;
  for (int i = 5; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--print")
      print = true;
    else if (arg == "--faults" && i + 1 < argc)
      faults = std::strtoul(argv[++i], nullptr, 10);
    else if (arg == "--only" && i + 1 < argc)
      only = argv[++i];
    else
      refused = true;
  }
  if (refused || faults == 0) {
    std::fprintf(stderr,
                 "%s <language> <bytes> <sites> <reps> [--faults <k>] "
                 "[--only <alteration>] [--print]\n",
                 argv[0]);
    return 2;
  }

  const Language* language = nullptr;
  for (const Language& l : kLanguages)
    if (l.name == lang) language = &l;
  if (language == nullptr) {
    std::fprintf(stderr, "%s: no language '%.*s'\n", argv[0],
                 static_cast<int>(lang.size()), lang.data());
    return 2;
  }
  const std::vector<Alteration> alterations = alterations_of(*language, only);
  if (alterations.empty()) {
    std::fprintf(stderr, "%s: no alteration '%.*s' in %.*s\n", argv[0],
                 static_cast<int>(only.size()), only.data(),
                 static_cast<int>(lang.size()), lang.data());
    return 2;
  }
  const std::unique_ptr<cursiv::Parser> engine = language->parser();
  const std::string text = language->generate(bytes);
  const std::vector<std::string> docs =
      altered(text, sites, faults, alterations);

  // The clean document, then the copies of each alteration: their best time
  // and the notes their trees carry.
  const auto group = [&](std::size_t doc) {
    return doc == 0 ? 0 : 1 + (doc - 1) % alterations.size();
  };
  std::vector<double> bestOf(1 + alterations.size(), 1e9);
  std::vector<std::size_t> notes(bestOf.size(), 0);
  std::uint64_t sink = 0;
  double best = 1e9;
  for (int r = 0; r < reps; ++r) {
    std::uint64_t h = 0;
    std::vector<double> spent(bestOf.size(), 0.0);
    const double s = bench::seconds_of([&] {
      for (std::size_t i = 0; i < docs.size(); ++i)
        spent[group(i)] += bench::seconds_of([&] {
          cursiv::ParseResult res = engine->parse(docs[i]);
          h ^= fingerprint(res.cst(), res.cst().diagnostics().empty());
          if (r == 0) notes[group(i)] += res.cst().diagnostics().size();
        });
    });
    sink ^= h;
    if (s < best) best = s;
    for (std::size_t g = 0; g < spent.size(); ++g)
      if (spent[g] < bestOf[g]) bestOf[g] = spent[g];
  }
  if (print)
    for (const std::string& d : docs) {
      cursiv::ParseResult res = engine->parse(d);
      std::printf("%zu %016llx\n", d.size(),
                  static_cast<unsigned long long>(
                      fingerprint(res.cst(), res.cst().diagnostics().empty())));
    }
  // A fault costs what its copy takes past the clean document.
  for (std::size_t g = 0; g < (sites == 0 ? 1 : bestOf.size()); ++g) {
    const double copies = g == 0 ? 1 : static_cast<double>(sites);
    const std::string_view name = g == 0 ? "clean" : alterations[g - 1].name;
    std::printf("%-8.*s %zu faults %.3f ms/doc %.2f us/fault %.1f notes/doc\n",
                static_cast<int>(name.size()), name.data(),
                g == 0 ? 0 : faults, bestOf[g] * 1e3 / copies,
                g == 0 ? 0.0
                       : (bestOf[g] / copies - bestOf[0]) * 1e6 /
                             static_cast<double>(faults),
                static_cast<double>(notes[g]) / copies);
  }
  std::printf("%.*s %zu docs %zu bytes best %.6f s sink %016llx\n",
              static_cast<int>(lang.size()), lang.data(), docs.size(),
              text.size(), best, static_cast<unsigned long long>(sink));
  return 0;
}
