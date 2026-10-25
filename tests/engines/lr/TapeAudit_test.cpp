#include "cells/generated/G/Module.hpp"
#include "cells/generated/G/Parser.hpp"
#include "cells/generated/G/Table.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/CstAudit.hpp"
#include "torture/generated/Torture/Module.hpp"
#include "unsettled/generated/G/Module.hpp"

// Domain A audit lane: CST integrity invariants over LR.

namespace {

int failures = 0;
int checked = 0;

void bad(const std::string& lang, const std::string& what,
         const std::string& text, const std::string& msg) {
  ++failures;
  if (failures > 40) return;
  std::string esc;
  for (char c : text) {
    if (c == '\n')
      esc += "\\n";
    else if (c == '\t')
      esc += "\\t";
    else
      esc += c;
  }
  std::printf("FAIL [%s] %s\n  text: <<%s>>\n  %s\n", lang.c_str(),
              what.c_str(), esc.c_str(), msg.c_str());
}

void audit(const std::string& lang, const std::string& what,
           const std::string& text, const cursiv::ParseResult& r) {
  ++checked;
  cursiv::test::audit_cst_structure(
      r, text, [&](const char* m) { bad(lang, what, text, m); });
}

std::string mutate(const std::string& seed, std::mt19937& rng, int faults) {
  static const std::string alpha = " \t\n{}[]()<>,;:=+-*/\"'@0123abcXY";
  std::string d = seed;
  for (int k = 0; k < faults && !d.empty(); ++k) {
    const int op = static_cast<int>(rng() % 3);
    const std::size_t p = rng() % (d.size() + (op == 0 ? 1 : 0));
    if (op == 0)
      d.insert(d.begin() + static_cast<long>(p), alpha[rng() % alpha.size()]);
    else if (op == 1 && p < d.size())
      d.erase(d.begin() + static_cast<long>(p));
    else if (p < d.size())
      d[p] = alpha[rng() % alpha.size()];
  }
  return d;
}

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

const char* kSeeds[] = {
    "go P { kind a = 1; }",
    "go P { kind a = 1 + 2 * 3; blueprint b <<<raw>>> [s] { 1, 2 } }",
    "go P { [s] { 1, \"x\", (1, 2) } kind t = (1+2)*3; }",
    "go P { // c\n kind a = 1; /* b */ blueprint z <<<q>>> }",
};

}  // namespace

namespace case_tapesplit {
// Domain A audit lane, split half: what a forked cell writes on the tape.

namespace {

int failures = 0;
int checked = 0;
int forests = 0;
int forestsValued = 0;
std::string firstForest;

void bad(const std::string& lang, const std::string& text,
         const std::string& msg) {
  ++failures;
  if (failures > 40) return;
  std::printf("FAIL [%s] <<%s>>\n  %s\n", lang.c_str(), text.c_str(),
              msg.c_str());
}

// The document a crash died on: printed and flushed before the parse, so an
// abort names its input.
const char* trace = std::getenv("TAPE_TRACE");
void say(const std::string& lang, const std::string& text) {
  if (trace == nullptr) return;
  std::fprintf(stderr, "TRY [%s] <<%s>>\n", lang.c_str(), text.c_str());
  std::fflush(stderr);
}

void audit(const std::string& lang, const std::string& text,
           const cursiv::ParseResult& r) {
  ++checked;
  cursiv::test::audit_cst_structure(r, text,
                                    [&](const char* m) { bad(lang, text, m); });
}

// The structural audit's question as a verdict: is the run a forest?
bool forest(const cursiv::ParseResult& r) {
  const cursiv::Cst& cst = r.cst();
  if (!r.accepted || !cst.has_entry() || cst.entry_root() >= cst.size())
    return false;
  const cursiv::NodeId root = cst.entry_root();
  const cursiv::NodeId lo = root + 1 - cst[root].subtreeSize;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    if (i >= lo && i <= root) continue;
    const cursiv::CstNode& x = cst[i];
    if (x.hidden || (x.recovered && x.subtreeSize == 1)) continue;
    return true;
  }
  return false;
}

std::string rules(const cursiv::Grammar& g, const cursiv::Cst& cst) {
  std::string out;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const std::string_view r = g.recordRule(cst[i]);
    const std::string_view f = g.recordFeature(cst[i]);
    out += std::to_string(cst[i].begin) + '-' + std::to_string(cst[i].end) +
           ':' + (r.empty() ? std::string("-") : std::string(r));
    if (!f.empty()) out += '/' + std::string(f);
    out += ' ';
  }
  return out;
}

std::string item(unsigned run, unsigned arm) {
  std::string s = "x";
  for (unsigned i = 0; i < run; ++i) s += " a";
  return s + " end" + std::to_string(arm);
}

// The same item with a comment between every token: the fork stands with
// trivia in hand, and each branch owes the run to its own segment.
std::string commented(unsigned run, unsigned arm) {
  std::string s = "/*o*/ x //l\n";
  for (unsigned i = 0; i < run; ++i) s += " /*" + std::to_string(i) + "*/ a";
  return s + " /*z*/ end" + std::to_string(arm) + " //t\n";
}

// Every byte of the text is under exactly one leaf, hidden or not: what a
// formatter reads back. Only asked of a clean parse -- a repair may drop
// text, and it says so in a diagnostic.
bool tiles(const cursiv::Cst& cst, const std::string& text) {
  cursiv::TextOffset at = 0;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    if (n.subtreeSize != 1) continue;
    if (n.begin == n.end) continue;
    if (n.begin < at) return false;
    for (cursiv::TextOffset p = at; p < n.begin; ++p)
      if (static_cast<unsigned char>(text[p]) > ' ') return false;
    at = n.end;
  }
  for (cursiv::TextOffset p = at; p < text.size(); ++p)
    if (static_cast<unsigned char>(text[p]) > ' ') return false;
  return true;
}

}  // namespace

int run() {

  const std::unique_ptr<cursiv::Parser> wide =
      tcells::generated::G::create_parser();
  const std::unique_ptr<cursiv::Parser> nest =
      tcells::generated::G::create_parser();
  const std::unique_ptr<cursiv::Parser> uns =
      tunsettled::generated::G::create_parser();

  const std::unique_ptr<cursiv::Parser> nsRc =
      tcells::generated::G::create_parser();
  const std::unique_ptr<cursiv::AstReflection> nsR =
      tcells::generated::G::create_ast_reflection();

  // The oracle: the text names its arm, the tree must hold that arm's node.
  // Read from the AST, so what is judged is what a consumer of the module
  // holds -- not an element id, which is each engine's own numbering.
  const auto arms_of = [](const cursiv::AstReflection& refl,
                          const cursiv::AstNode* v) {
    std::string s;
    if (v == nullptr) return std::string("<no ast>");
    refl.visitMembers(
        *v, [&](std::string_view, std::uint32_t, cursiv::AstNode& kid) {
          s += refl.getTypeMetaData(kid.type).name;
          s += ' ';
        });
    return s;
  };

  for (unsigned run : {0u, 1u, 60u, 130u})
    for (unsigned k = 0; k < 17; ++k) {
      const std::string t = item(run, k);
      say("ns", t);
      const cursiv::ParseResult r = nsRc->parse(t);
      audit("namedsplit", t, r);
      if (!r.accepted || r.cst().recovered()) {
        bad("namedsplit", t, "R1 a document the grammar spells is refused");
        continue;
      }
      const std::string want = "A" + std::to_string(k) + " ";
      const std::string got = arms_of(*nsR, r.value);
      if (got != want)
        bad("namedsplit", t,
            "R2 the elected reading named " + got + "-- the text names " +
                want + "(records " + rules(nsRc->getGrammar(), r.cst()) + ")");
    }

  for (unsigned a : {0u, 1u, 8u, 15u, 16u})
    for (unsigned b : {0u, 1u, 8u, 15u, 16u})
      for (unsigned ra : {0u, 60u}) {
        const std::string t = item(ra, a) + ";" + item(1, b);
        say("ns", t);
        const cursiv::ParseResult r = nsRc->parse(t);
        audit("namedsplit", t, r);
        if (!r.accepted || r.cst().recovered()) {
          bad("namedsplit", t, "R3 a document the grammar spells is refused");
          continue;
        }
        const std::string want =
            "A" + std::to_string(a) + " A" + std::to_string(b) + " ";
        const std::string got = arms_of(*nsR, r.value);
        if (got != want)
          bad("namedsplit", t,
              "R4 the elected reading named " + got + "-- the text names " +
                  want);
      }

  // Three items, so a fork stands over a fork and a collapse runs between
  // them.
  for (unsigned a : {0u, 8u, 15u, 16u})
    for (unsigned b : {0u, 8u, 16u})
      for (unsigned c : {0u, 16u})
        for (unsigned ra : {60u}) {
          const std::string t =
              item(ra, a) + ";" + item(60, b) + ";" + item(1, c);
          say("ns", t);
          const cursiv::ParseResult r = nsRc->parse(t);
          audit("namedsplit", t, r);
          if (!r.accepted || r.cst().recovered()) {
            bad("namedsplit", t, "R5 a document the grammar spells is refused");
            continue;
          }
          const std::string want = "A" + std::to_string(a) + " A" +
                                   std::to_string(b) + " A" +
                                   std::to_string(c) + " ";
          const std::string got = arms_of(*nsR, r.value);
          if (got != want)
            bad("namedsplit", t,
                "R6 the elected reading named " + got + "-- the text names " +
                    want);
        }

  // A fork standing with trivia in hand: each branch owes the run to its own
  // segment, and the elected one must hold it once.
  for (unsigned run : {0u, 1u, 60u})
    for (unsigned k = 0; k < 17; ++k) {
      const std::string t = commented(run, k);
      say("ns", t);
      const cursiv::ParseResult r = nsRc->parse(t);
      audit("namedsplit-trivia", t, r);
      if (!r.accepted || r.cst().recovered()) {
        bad("namedsplit-trivia", t,
            "T1 a document the grammar spells is refused");
        continue;
      }
      const std::string want = "A" + std::to_string(k) + " ";
      const std::string got = arms_of(*nsR, r.value);
      if (got != want)
        bad("namedsplit-trivia", t,
            "T2 the elected reading named " + got + "-- the text names " +
                want);
      if (!tiles(r.cst(), t))
        bad("namedsplit-trivia", t,
            "T3 the leaves do not tile the text: " +
                rules(nsRc->getGrammar(), r.cst()));
    }
  for (unsigned a = 0; a < 17; a += 4)
    for (unsigned ra : {1u, 60u}) {
      const std::string t = commented(ra, a) + ";" + commented(60, 16 - a);
      say("ns", t);
      const cursiv::ParseResult r = nsRc->parse(t);
      audit("namedsplit-trivia", t, r);
      if (!r.accepted || r.cst().recovered()) {
        bad("namedsplit-trivia", t,
            "T4 a document the grammar spells is refused");
        continue;
      }
      const std::string want =
          "A" + std::to_string(a) + " A" + std::to_string(16 - a) + " ";
      if (arms_of(*nsR, r.value) != want)
        bad("namedsplit-trivia", t,
            "T5 the elected reading named " + arms_of(*nsR, r.value) +
                "-- the text names " + want);
      if (!tiles(r.cst(), t))
        bad("namedsplit-trivia", t, "T6 the leaves do not tile the text");
    }

  for (unsigned run : {0u, 60u, 200u})
    for (unsigned k : {0u, 15u, 16u, 31u, 32u, 39u}) {
      const std::string t = "wide " + item(run, k);
      const cursiv::ParseResult r = wide->parse(t);
      audit("wideunsettled", t, r);
      if (!r.accepted || r.cst().recovered())
        bad("wideunsettled", t, "W1 a document the grammar spells is refused");
    }

  for (unsigned k : {0u, 15u, 16u, 31u, 32u, 39u})
    for (const char* tail : {"b", "c"}) {
      const std::string t = "nested x y end" + std::to_string(k) + " " + tail;
      const cursiv::ParseResult r = nest->parse(t);
      audit("nestedcands", t, r);
      if (!r.accepted || r.cst().recovered())
        bad("nestedcands", t, "N1 a document the grammar spells is refused");
    }

  // A run of skips opening at the fork's own base: the widened error leaf is
  // the one record a child segment shadows, and the guard that keeps it out
  // of an ancestor stands exactly there.
  for (unsigned junk : {1u, 8u})
    for (unsigned run : {0u, 60u})
      for (unsigned k = 0; k < 17; k += 4) {
        const std::string g(junk, '#');
        for (const std::string& t :
             {"x " + g + item(run, k).substr(1), item(run, k) + " " + g,
              "x" + g + item(run, k).substr(1),
              item(run, k).substr(0, item(run, k).size() - 1) + g,
              "x " + g + " " + g + item(run, k).substr(1)}) {
          say("ns", t);
          const cursiv::ParseResult r = nsRc->parse(t);
          audit("namedsplit-skip", t, r);
          if (r.accepted && !tiles(r.cst(), t) && r.cst().diagnostics().empty())
            bad("namedsplit-skip", t,
                "S1 a clean tree whose leaves do not tile the text");
        }
      }

  // A repair on top of a split: what the fork left shared, the elected
  // reading then writes on.
  static const std::string alpha = " abcdefimnxy0123456789;end/*";
  std::mt19937 rng(20260907u);
  const std::string seeds[] = {item(0, 3),
                               item(1, 16),
                               item(120, 0),
                               item(3, 9) + ";" + item(1, 16),
                               item(60, 5) + ";" + item(0, 2),
                               commented(2, 7),
                               commented(60, 16)};
  for (const std::string& s : seeds)
    for (int faults = 1; faults <= 6; ++faults)
      for (int t = 0; t < 16; ++t) {
        std::string d = s;
        for (int f = 0; f < faults && !d.empty(); ++f) {
          const int op = static_cast<int>(rng() % 3);
          const std::size_t p = rng() % (d.size() + (op == 0 ? 1 : 0));
          if (op == 0)
            d.insert(d.begin() + static_cast<long>(p),
                     alpha[rng() % alpha.size()]);
          else if (op == 1 && p < d.size())
            d.erase(d.begin() + static_cast<long>(p));
          else if (p < d.size())
            d[p] = alpha[rng() % alpha.size()];
        }
        say("mut", d);
        // One reading of this grammar, read twice: a forest is a defect
        // wherever it comes from, and only one carrying a value reaches the
        // builder.
        const cursiv::ParseResult ns = nsRc->parse(d);
        if (forest(ns)) {
          ++forests;
          if (ns.value != nullptr) {
            ++forestsValued;
            if (firstForest.empty()) firstForest = d;
          }
        }
        audit("namedsplit-mut", d, ns);
        const std::string wideText = "wide " + d, nestText = "nested " + d;
        audit("wideunsettled-mut", wideText, wide->parse(wideText));
        audit("nestedcands-mut", nestText, nest->parse(nestText));
        audit("unsettled-mut", d, uns->parse(d));
      }

  // What a caret reads off a split tree: the state a record settled in. Asked
  // at every offset of the documents the forks were opened over.
  {
    std::vector<cursiv::NextFeature> out;
    std::size_t asked = 0;
    for (unsigned run : {0u, 60u})
      for (unsigned k : {0u, 16u})
        for (const std::string& t :
             {item(run, k), commented(run, k), "x ### " + item(run, k),
              item(run, k) + " ;", item(run, k) + ";" + item(1, k)}) {
          say("ns", t);
          const cursiv::ParseResult r = nsRc->parse(t);
          for (cursiv::TextOffset p = 0; p <= t.size(); ++p) {
            // Sweep short documents; sample the identical middle repetitions
            // of long ones, keeping every offset at both ends.
            if (run != 0 && p > 32 && p + 32 < t.size() && p % 16 != 0)
              continue;
            out.clear();
            nsRc->nextFeaturesAt(r.cst(), p, p, out);
            ++asked;
          }
        }
    std::printf("tape_split: %zu caret questions asked\n", asked);
  }

  std::printf(
      "tape_split: forests %d (with an ast: %d)\n"
      "tape_split: first forest with an ast <<%s>>\n",
      forests, forestsValued, firstForest.c_str());
  std::printf("tape_split: %d trees checked, %d failures\n", checked, failures);
  return failures == 0 ? 0 : 1;
}
}  // namespace case_tapesplit

int main() {
  int movedStatus = 0;
  movedStatus |= case_tapesplit::run();
  const std::unique_ptr<cursiv::Parser> a =
      tort::generated::Torture::create_parser();

  // The torture corpus and mutations through one representative module.
  const std::string dir = CURSIV_TAPE_CORPUS;
  std::vector<std::string> files;
  for (const auto& e : std::filesystem::directory_iterator(dir))
    if (e.path().extension() == ".trt") files.push_back(e.path().string());
  std::sort(files.begin(), files.end());
  for (const std::string& f : files) {
    const std::string text = slurp(f);
    audit("torture-A", "corpus " + f, text, a->parse(text));
  }

  // Exhaustive one-byte mutations of the seeds.
  static const std::string alpha = " \t\n{}[]()<>,;:=+-*/\"'@0123abcXY";
  for (const char* s : kSeeds) {
    const std::string seed = s;
    for (std::size_t p = 0; p <= seed.size(); ++p) {
      for (const char c : alpha) {
        std::string d = seed;
        d.insert(d.begin() + static_cast<long>(p), c);
        const cursiv::ParseResult x = a->parse(d);
        audit("torture-A", "ins", d, x);
      }
      if (p >= seed.size()) continue;
      {
        std::string d = seed;
        d.erase(d.begin() + static_cast<long>(p));
        const cursiv::ParseResult x = a->parse(d);
        audit("torture-A", "del", d, x);
      }
      for (const char c : alpha) {
        std::string d = seed;
        d[p] = c;
        const cursiv::ParseResult x = a->parse(d);
        audit("torture-A", "rep", d, x);
      }
    }
  }

  // Many faults at once: what forces forks, collapses and compactions.
  std::mt19937 rng(20260907u);
  for (const char* s : kSeeds)
    for (int faults = 1; faults <= 8; ++faults)
      for (int t = 0; t < 100; ++t) {
        const std::string d = mutate(s, rng, faults);
        const cursiv::ParseResult x = a->parse(d);
        audit("torture-A", "mut" + std::to_string(faults), d, x);
      }

  std::printf("tape_audit: %d trees checked, %d failures\n", checked, failures);
  return (failures == 0 ? 0 : 1) | movedStatus;
}
