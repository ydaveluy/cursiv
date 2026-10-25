#include "oracle/rgram_lane.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/AstText.hpp"
#include "oracle/RgramCfg.hpp"
#include "oracle/RgramGrammar.hpp"

namespace cursiv {
namespace {

// A parse gets this long before the lane calls it a hang.
constexpr unsigned kWatchSeconds = 10;

// Written before the parse: the handler may only call `write`.
char gHang[512];
std::size_t gHangLen = 0;

extern "C" void rgram_hang(int) {
  (void)!::write(2, gHang, gHangLen);
  ::_exit(3);
}

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
  std::signal(SIGALRM, rgram_hang);
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
    ::alarm(kWatchSeconds);
    const ParseResult r = parser.parse(d.text);
    ::alarm(0);
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
    test::ast_of(reflection, *r.value, got);
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
  return bad == 0 ? 0 : 1;
}

}  // namespace cursiv
