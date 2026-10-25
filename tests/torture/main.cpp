// The torture lane: a grammar built to break error recovery. Every row is
// printed (nothing is pinned -- this lane is exploration, not a gate) plus
// two automated invariants that are gates: the tree stays structurally sane,
// and a bounded random-mutation pass never crashes the engine.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/CstAudit.hpp"
#include "cursiv/test/MutationOracle.hpp"
#include "cursiv/test/RecoveryLane.hpp"
#include "cursiv/test/SkippedLeaf.hpp"
#include "optionals/generated/OptionalsG/Module.hpp"
#include "torture/generated/Torture/Module.hpp"

namespace {

void check_tree(const cursiv::ParseResult& r, const std::string& text,
                const std::string& name) {
  cursiv::test::audit_cst_structure(r, text, [&](const char* m) {
    std::printf("  STRUCT %s: %s\n", name.c_str(), m);
    ++failures;
  });
}

// The coverage identity, and it is a gate: every span the elected reading
// threw away lies inside some diagnostic's span. A skipped token with nothing
// said about it is the one outcome an editor cannot work with.
void check_coverage(const cursiv::Parser& engine, const std::string& text,
                    const std::string& name) {
  const cursiv::ParseResult result = engine.parse(text);
  const cursiv::Cst& cst = result.cst();
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    if (n.subtreeSize != 1 ||
        !cursiv::test::skipped_leaf(engine.getGrammar(), n) || n.hidden)
      continue;
    bool covered = false;
    for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
      if (d.begin <= n.begin && n.end <= d.end) {
        covered = true;
        break;
      }
    if (!covered) {
      std::printf("  COVER %s: span [%u,%u) has no diagnostic\n", name.c_str(),
                  n.begin, n.end);
      ++failures;
    }
  }
}

// The other half of the coverage identity, and it is a gate: a diagnostic
// that claims text (deleted, replaced, incomplete) covers only text the
// elected reading gave up -- an error leaf, or a region wrapped under a
// repaired reduce. A live leaf inside such a span is a diagnostic that lies.
void check_claims(const cursiv::Parser& engine, const std::string& text,
                  const std::string& name) {
  const cursiv::ParseResult result = engine.parse(text);
  const cursiv::Cst& cst = result.cst();
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    if (n.subtreeSize != 1 || n.hidden || n.recovered) continue;
    bool wrapped = false;
    for (cursiv::NodeId j = i + 1; j < cst.size() && !wrapped; ++j)
      if (cst[j].recovered && cst[j].subtreeSize > 1 &&
          j - cst[j].subtreeSize + 1 <= i)
        wrapped = true;
    if (wrapped) continue;
    for (const cursiv::ParseDiagnostic& d : cst.diagnostics()) {
      if (d.kind == cursiv::DiagnosticKind::inserted ||
          d.kind == cursiv::DiagnosticKind::conversion_error)
        continue;
      if (d.begin < n.end && n.begin < d.end) {
        std::printf("  CLAIM %s: diagnostic [%u,%u) covers live leaf [%u,%u)\n",
                    name.c_str(), d.begin, d.end, n.begin, n.end);
        ++failures;
      }
    }
  }
}

// The recovery's contract, gated: an accepted document has a value.
void check_value(const cursiv::ParseResult& result, const std::string& name) {
  if (!result.cst().accepted() || result.value != nullptr) return;
  std::printf("  FAIL %s: accepted but the AST value is null\n", name.c_str());
  ++failures;
}

void row(const cursiv::Parser& engine, const std::string& text,
         const std::string& name, const cursiv::test::Proj* baseline) {
  const cursiv::test::Proj p = cursiv::test::project(engine, text);
  const int inv =
      baseline ? cursiv::test::invented(p.tokens, baseline->tokens) : 0;
  const std::size_t validTokens =
      baseline ? baseline->tokens.size() : p.tokens.size();
  std::printf("%-38s %s\n", name.c_str(),
              cursiv::test::signature(p, validTokens, inv).c_str());
  const cursiv::ParseResult result = engine.parse(text);
  check_tree(result, text, name);
  check_coverage(engine, text, name);
  check_claims(engine, text, name);
  check_value(result, name);
}

// A bounded mutation pass over the valid document: single-byte deletions,
// insertions and replacements at every offset, plus two-mutation
// combinations at fixed strides. Every single-mutation document is also held
// to the mutation oracle's floor.
void mutate_pass(const cursiv::Parser& engine, const std::string& seed) {
  int tried = 0;
  int problems_before = failures;
  const auto one = [&](const std::string& doc, char op, std::size_t pos,
                       char byte) {
    ++tried;
    const cursiv::ParseResult r = engine.parse(doc);
    const std::string name = cursiv::test::mutation_name(op, pos, byte);
    check_tree(r, doc, "mutate:" + name);
    cursiv::test::mutation_floor("torture", engine.getGrammar(), r, doc, name);
  };
  for (std::size_t pos = 0; pos <= seed.size(); ++pos) {
    for (char c : std::string_view(cursiv::test::kMutationAlphabet)) {
      std::string doc = seed;
      doc.insert(doc.begin() + static_cast<long>(pos), c);
      one(doc, 'i', pos, c);
    }
    if (pos < seed.size()) {
      std::string doc = seed;
      doc.erase(doc.begin() + static_cast<long>(pos));
      one(doc, 'd', pos, 0);
      for (char c : std::string_view(cursiv::test::kMutationAlphabet)) {
        std::string rep = seed;
        rep[pos] = c;
        one(rep, 'r', pos, c);
      }
    }
  }
  // Two-mutation combinations: two independent single-byte deletions at
  // random offsets, seeded so a failing pair reproduces without saving it.
  std::mt19937 rng(0xC0FFEE);
  std::uniform_int_distribution<std::size_t> pick(
      0, seed.size() > 1 ? seed.size() - 2 : 0);
  for (int i = 0; i < 2000 && seed.size() > 2; ++i) {
    std::size_t a = pick(rng);
    std::size_t b = pick(rng);
    if (a == b) continue;
    if (a < b) std::swap(a, b);
    std::string doc = seed;
    doc.erase(doc.begin() + static_cast<long>(b));
    doc.erase(doc.begin() + static_cast<long>(a));
    ++tried;
    const cursiv::ParseResult r = engine.parse(doc);
    check_tree(r, doc,
               "mutate:del2@" + std::to_string(a) + "," + std::to_string(b));
  }
  std::printf("mutate_pass: %d documents tried, %d structural problem(s)\n",
              tried, failures - problems_before);
}

// The recycling break: `var X = ;` next to a well-formed `var Y = 2;`. The
// second declaration is not part of the fault, so its tokens survive it --
// a reading that steals them to patch the first must lose the election.
void recycled_neighbour() {
  const std::unique_ptr<cursiv::Parser> parser =
      topt::generated::OptionalsG::create_parser();
  const cursiv::Parser& engine = *parser;
  const std::string text = "recycle var X = ; var Y = 2;";
  const cursiv::test::Proj p = cursiv::test::project(engine, text);
  std::printf("%-38s %s\n", "recycled_neighbour",
              cursiv::test::signature(p, p.tokens.size(), 0).c_str());
  check_tree(engine.parse(text), text, "recycled_neighbour");
  check_coverage(engine, text, "recycled_neighbour");
  bool ySurvives = false;
  for (const std::string& t : p.tokens)
    if (t == "Y") ySurvives = true;
  if (!ySurvives) {
    std::printf(
        "  FAIL recycled_neighbour: Y was fed to the previous repair\n");
    ++failures;
  }
  // The twin break with no anchor after Y: the freeze keeps its rivals, so
  // the reading that saves Y wins the cost race there too.
  const std::string twin = "recycle var X = ; var Y = ;";
  const cursiv::test::Proj q = cursiv::test::project(engine, twin);
  std::printf("%-38s %s\n", "recycled_neighbour_twin",
              cursiv::test::signature(q, q.tokens.size(), 0).c_str());
  check_coverage(engine, twin, "recycled_neighbour_twin");
  bool yTwin = false;
  for (const std::string& t : q.tokens)
    if (t == "Y") yTwin = true;
  if (!yTwin) {
    std::printf("  FAIL recycled_neighbour_twin: Y did not survive\n");
    ++failures;
  }
}

void a_stray_quote_does_not_invent_a_declaration(
    const cursiv::Parser& engine, const std::string& seed) {
  auto text = seed;
  const auto at = text.find("4 + 5");
  CHECK(at != std::string::npos);
  if (at == std::string::npos) return;
  text.insert(at + 1, 1, '"');
  const auto result = cursiv::test::project(engine, text);
  CHECK(result.accepted && result.recovered);
  CHECK(std::find(result.tokens.begin(), result.tokens.end(), "tag") ==
        result.tokens.end());
  CHECK(std::find(result.tokens.begin(), result.tokens.end(), "width") !=
        result.tokens.end());
}

}  // namespace

void torture_dangling();
void torture_election();
void torture_optionals();
void torture_boundary();

int main() {
  const std::unique_ptr<cursiv::Parser> parser =
      tort::generated::Torture::create_parser();
  const cursiv::Parser& engine = *parser;

  const std::vector<std::string> names = {
      "00_valid.trt",
      "01_through_unclosed_eof.trt",
      "02_through_unclosed_then_faults.trt",
      "03_through_near_miss_closer.trt",
      "04_tight_region_leading_space.trt",
      "05_tight_region_trailing_space.trt",
      "06_deep_nest_unclosed.trt",
      "07_mismatched_closers.trt",
      "08_orphan_closer.trt",
      "09_first_byte_fault.trt",
      "10_single_byte_doc.trt",
      "11_last_byte_fault.trt",
      "12_last_byte_extra.trt",
      "13_burst_tight.trt",
      "14_burst_spread.trt",
      "15_typo_short_kw_del.trt",
      "16_typo_short_kw_transpose.trt",
      "17_typo_long_kw_transpose.trt",
      "18_typo_long_kw_headchar.trt",
      "19_choice_del_vs_ins_op.trt",
      "20_choice_del_vs_ins_comma.trt",
      "21_choice_missing_comma.trt",
      "22_control_plain_closer_eof.trt",
      "23_control_typo_then_eof.trt",
      "24_recycled_decl.trt",
  };

  const std::string validText = cursiv::test::read_file(
      std::string(CURSIV_TORTURE_CORPUS) + "/00_valid.trt");
  const cursiv::test::Proj valid = cursiv::test::project(engine, validText);
  if (!valid.accepted || valid.recovered) {
    std::printf("FAIL torture: the valid document is not taken cleanly\n");
    ++failures;
  }

  for (const std::string& name : names) {
    const std::string text = cursiv::test::read_file(
        std::string(CURSIV_TORTURE_CORPUS) + "/" + name);
    if (text.empty() && name != "00_valid.trt") {
      std::printf("FAIL torture/%s: document not found\n", name.c_str());
      ++failures;
      continue;
    }
    row(engine, text, name, &valid);
  }

  recycled_neighbour();
  a_stray_quote_does_not_invent_a_declaration(engine, validText);
  mutate_pass(engine, validText);

  torture_dangling();
  torture_election();
  torture_optionals();
  torture_boundary();

  return cursiv::test::report("torture_test");
}
