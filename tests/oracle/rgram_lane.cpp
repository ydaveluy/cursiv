#include "oracle/rgram_lane.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#ifdef SIGALRM
#include <unistd.h>
#endif

#include "cursiv/CstInvariants.hpp"
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

// The alarm spans both parser calls and their comparison.
char gHang[512];
std::size_t gHangLen = 0;
enum Phase { Reading, FullElection, Comparing };
volatile std::sig_atomic_t gPhase = Reading;

// Without alarms, as on Windows, the test's timeout is what stops a hang.
#ifdef SIGALRM
extern "C" void rgram_hang(int) {
  constexpr std::string_view phases[] = {"default parser: ", "full election: ", "comparison: "};
  const auto phase = phases[gPhase];
  (void)!::write(2, phase.data(), phase.size());
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

bool same_parse(const ParseResult& a, const ParseResult& b) {
  const Cst& x = a.cst();
  const Cst& y = b.cst();
  if (a.accepted != b.accepted || a.error != b.error || x.entryRoot() != y.entryRoot() ||
      x.size() != y.size()) return false;
  for (NodeId i = 0; i < x.size(); ++i)
    if (x[i].begin != y[i].begin || x[i].end != y[i].end ||
        x[i].subtreeSize != y[i].subtreeSize || x[i].engine() != y[i].engine()) return false;
  const auto fields = [](const ParseDiagnostic& d) {
    return std::tie(d.kind, d.mixedIndent, d.element, d.also, d.begin, d.end, d.message);
  };
  return std::ranges::equal(a.diagnostics, b.diagnostics, {}, fields, fields);
}

}  // namespace

int rgram_lane(unsigned seed, const Parser& parser, const AstReflection& reflection,
               unsigned documents, const Parser* election) {
  unsigned bad = 0, inLanguage = 0, trees = 0;
  // The oracle's own contract: inside a region, a blank before the end is no end.
  RgramGrammar end;
  end.rules.push_back({.kind = RgramKind::skip,
                       .children = {{.children = {{.kind = RgramKind::tok, .tok = 'a'},
                                                  {.kind = RgramKind::cond}}}}});
  for (const std::string gap : {"", " ", "\n"})
    if (RgramCfg(end).read({"a" + gap, {'a'}, {"", gap}}, false).accepted != gap.empty()) {
      std::printf("rgram oracle: tight(a + eof) on \"a%s\" answers wrongly\n", shown(gap).c_str());
      ++bad;
    }
  RgramExpr conditions;
  conditions.children = {{.kind = RgramKind::tok, .tok = 'a'},
                         {.kind = RgramKind::cond, .cond = RgramCond::notNone},
                         {.kind = RgramKind::cond, .tok = 'b', .cond = RgramCond::andTok}};
  RgramGrammar boundary;
  boundary.rules.push_back({.children = {{.kind = RgramKind::skip, .children = {conditions}},
                                        {.kind = RgramKind::tok, .tok = 'b'}}});
  const RgramCfg outside(boundary);
  for (const std::string gap : {"", " ", "\n"})
    if (outside.read({"a" + gap + "b", {'a', 'b'}, {"", gap, ""}}, false).accepted == gap.empty()) {
      std::printf("rgram oracle: tight(a + !gap::none + &b) + b on \"a%sb\" answers wrongly\n", shown(gap).c_str());
      ++bad;
    }
  const RgramGrammar g = RgramGrammar::draw(seed);
  const RgramCfg cfg(g);
  const std::vector<RgramDocument> docs =
      cfg.documents(std::uint64_t{seed} * 7919u + 1u, documents);
#ifdef SIGALRM
  std::signal(SIGALRM, rgram_hang);
#endif
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
    gPhase = Reading;
    watch(kWatchSeconds);
    const ParseResult r = parser.parse(d.text);
    if (election) {
      gPhase = FullElection;
      const ParseResult full = election->parse(d.text);
      gPhase = Comparing;
      if (!same_parse(r, full)) {
        ++bad;
        std::printf("rgram %u: document %zu \"%s\": prediction and full election disagree\n",
                    seed, i, shown(d.text).c_str());
      }
    }
    watch(0);
    const auto cst = fuzzer::detail::CstInvariants{}.run(r.cst(), d.text);
    if (!cst.ok) {
      ++bad;
      std::printf("rgram %u: document %zu: CST record %u: %s\n", seed, i, cst.at, cst.why.c_str());
    }
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
