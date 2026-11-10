#include "oracle/rgram_lane.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifdef SIGALRM
#include <unistd.h>
#endif

#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "oracle/RgramCfg.hpp"
#include "oracle/RgramGrammar.hpp"
#include "support/AstText.hpp"

namespace cursiv {
namespace {

// A parse gets this long before the test calls it a hang.
constexpr unsigned kWatchSeconds = 10;

// Written before the parse: the handler may only call `write`.
char gHang[512];
std::size_t gHangLen = 0;

// Without alarms, as on Windows, the test's timeout is what stops a hang.
#ifdef SIGALRM
extern "C" void rgram_hang(int) {
  (void)!::write(2, gHang, gHangLen);
  ::_exit(3);
}

void watch(unsigned seconds) { ::alarm(seconds); }
#else
void watch(unsigned) {}
#endif

std::string shown(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c == '\n')
      out += "\\n";
    else
      out += c;
  }
  return out;
}

constexpr const char* kKinds[] = {"inserted",   "deleted",
                                  "replaced",   "incomplete",
                                  "conversion", "bounded"};

std::string told(const ParseResult& r) {
  std::string out;
  for (const ParseDiagnostic& d : r.diagnostics) {
    const auto k = static_cast<unsigned>(d.kind);
    out += std::string(k < std::size(kKinds) ? kKinds[k] : "?") + " " +
           std::to_string(d.begin) + ".." + std::to_string(d.end) + "; ";
  }
  return out;
}

std::string bare(std::string_view tree) {
  std::string out;
  for (const char c : tree)
    if (c != ' ') out += c;
  return out;
}

}  // namespace

int rgram_lane(unsigned seed, const Parser& parser,
               const AstReflection& reflection, unsigned documents) {
  const RgramGrammar g = RgramGrammar::draw(seed);
  const RgramCfg cfg(g);
  const std::vector<RgramDocument> docs =
      cfg.documents(std::uint64_t{seed} * 7919u + 1u, documents);
#ifdef SIGALRM
  std::signal(SIGALRM, rgram_hang);
#endif
  unsigned bad = 0, inLanguage = 0, trees = 0;
  for (std::size_t i = 0; i < docs.size(); ++i) {
    const RgramDocument& d = docs[i];
    const RgramVerdict o = cfg.read(d, g.features.ast);
    inLanguage += o.accepted ? 1 : 0;
    trees += o.tree.empty() ? 0 : 1;
    const int len = std::snprintf(
        gHang, sizeof gHang,
        "rgram %u: document %zu did not finish in %u s: \"%s\"\n", seed, i,
        kWatchSeconds, shown(d.text).c_str());
    gHangLen = len < 0 ? 0 : std::min<std::size_t>(len, sizeof gHang - 1);
    watch(kWatchSeconds);
    const ParseResult r = parser.parse(d.text);
    watch(0);
    const bool clean = r.accepted && r.diagnostics.empty();
    if (clean != o.accepted || (clean && r.value == nullptr)) {
      ++bad;
      std::printf("rgram %u: document %zu \"%s\": the parser %s it (%s), "
                  "the grammar %s it\n",
                  seed, i, shown(d.text).c_str(),
                  clean ? "reads" : "repairs", told(r).c_str(),
                  o.accepted ? "derives" : "does not derive");
      continue;
    }
    if (!clean || o.tree.empty()) continue;
    std::string got;
    support::ast_of(reflection, *r.value, got);
    if (bare(got) == bare(o.tree)) continue;
    ++bad;
    std::printf("rgram %u: document %zu \"%s\": the parser builds\n  %s\n"
                "the grammar's one tree is\n  %s\n",
                seed, i, shown(d.text).c_str(), got.c_str(), o.tree.c_str());
  }
  std::printf("rgram %u (%.*s): %zu documents, %u in the language, %u with one "
              "tree, %u disagreements\n",
              seed, static_cast<int>(g.family.size()), g.family.data(),
              docs.size(), inLanguage, trees, bad);
  if (bad != 0) return 1;
  // Agreement on nothing proves nothing: with no disagreement, every tree the
  // oracle found was compared.
  if (inLanguage == 0 || (g.features.ast && trees == 0)) {
    std::printf("rgram %u: nothing judged, %s\n", seed,
                inLanguage == 0 ? "no document is in the language"
                                : "no document has one tree");
    return kRgramUnjudged;
  }
  return 0;
}

}  // namespace cursiv
