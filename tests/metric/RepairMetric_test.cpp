// The recovery metric on cases built by hand: the pairing of faults and
// diagnostics, the alignment of the author's text, and the overlap of two
// trees. What a parser answers is not judged here.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/fuzz/CursivActions.hpp"
#include "cursiv/fuzz/SeededSource.hpp"
#include "recovery_metric/support/AstOverlap.hpp"
#include "recovery_metric/support/FaultCorpus.hpp"
#include "recovery_metric/support/FaultPairing.hpp"
#include "recovery_metric/support/FaultWindow.hpp"
#include "recovery_metric/support/RepairCase.hpp"
#include "recovery_metric/support/RepairFault.hpp"
#include "recovery_metric/support/RepairTwin.hpp"
#include "recovery_metric/support/TextKept.hpp"
#include "cursiv/test/Check.hpp"
#include "domainmodel/core/DomainModelGrammar.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"

using cursiv::test::failures;

namespace {

cursiv::FaultWindow window(cursiv::TextOffset lo, cursiv::TextOffset hi,
                           cursiv::TextOffset at) {
  cursiv::FaultWindow w;
  w.lo = lo;
  w.hi = hi;
  w.at = at;
  w.end = at;
  return w;
}

cursiv::ParseDiagnostic said(cursiv::DiagnosticKind kind,
                             cursiv::TextOffset begin, cursiv::TextOffset end,
                             cursiv::ElementId element = cursiv::kNoElement) {
  cursiv::ParseDiagnostic d;
  d.kind = kind;
  d.begin = begin;
  d.end = end;
  d.element = element;
  return d;
}

cursiv::CstLeaf leaf(cursiv::TextOffset begin, cursiv::TextOffset end,
                     bool recovered = false) {
  static cursiv::NodeId id = 0;
  return cursiv::CstLeaf{id++, begin, end, false, recovered};
}

using Kind = cursiv::DiagnosticKind;

void one_diagnostic_more_is_a_parasite() {
  const std::vector<cursiv::FaultWindow> w{window(10, 20, 15)};
  std::vector<cursiv::ParseDiagnostic> d{said(Kind::inserted, 15, 15)};
  auto p = cursiv::FaultPairing::pair(w, d);
  CHECK(p.found == 1 && p.missed == 0 && p.parasites == 0);
  d.push_back(said(Kind::deleted, 12, 13));
  p = cursiv::FaultPairing::pair(w, d);
  CHECK(p.found == 1 && p.parasites == 1);
  d.push_back(said(Kind::deleted, 100, 101));
  p = cursiv::FaultPairing::pair(w, d);
  CHECK(p.found == 1 && p.parasites == 2);
}

// The nearest diagnostic goes first, and gives way when another fault has
// nothing else within reach.
void the_pairing_answers_the_most_faults() {
  const std::vector<cursiv::FaultWindow> w{window(10, 20, 14),
                                           window(12, 22, 21)};
  const std::vector<cursiv::ParseDiagnostic> d{said(Kind::inserted, 14, 14),
                                               said(Kind::inserted, 19, 19)};
  const auto p = cursiv::FaultPairing::pair(w, d);
  CHECK(p.found == 2 && p.parasites == 0);
  CHECK(p.answer[0] == 0 && p.answer[1] == 1);
  const std::vector<cursiv::FaultWindow> tight{window(10, 20, 14),
                                               window(14, 14, 14)};
  const auto q = cursiv::FaultPairing::pair(tight, d);
  CHECK(q.found == 2 && q.answer[1] == 0 && q.answer[0] == 1);
}

// Over drawn windows and diagnostics: adding one never lowers what was found
// nor what was left over.
void a_diagnostic_never_buys_back_a_parasite() {
  cursiv::fuzzer::SeededSource rng(0x9a17);
  for (int round = 0; round < 400; ++round) {
    std::vector<cursiv::FaultWindow> w;
    const unsigned faults = 1 + static_cast<unsigned>(rng.next() % 5);
    for (unsigned f = 0; f < faults; ++f) {
      const auto lo = static_cast<cursiv::TextOffset>(rng.next() % 100);
      const auto hi = lo + static_cast<cursiv::TextOffset>(rng.next() % 12);
      w.push_back(window(lo, hi, lo + (hi - lo) / 2));
    }
    std::vector<cursiv::ParseDiagnostic> d;
    auto before = cursiv::FaultPairing::pair(w, d);
    for (int k = 0; k < 8; ++k) {
      const auto b = static_cast<cursiv::TextOffset>(rng.next() % 110);
      const auto kind = static_cast<Kind>(rng.next() % 4);
      d.push_back(said(kind, b, b + static_cast<cursiv::TextOffset>(rng.next() % 30)));
      const auto after = cursiv::FaultPairing::pair(w, d);
      CHECK(after.found >= before.found);
      CHECK(after.parasites >= before.parasites);
      CHECK(after.found + after.parasites == d.size());
      before = after;
    }
  }
}

// An abandonment reaches a fault at one of its two ends, never through the
// text it covers.
void a_wide_abandonment_answers_by_its_ends() {
  const std::vector<cursiv::FaultWindow> w{window(50, 60, 55)};
  CHECK(cursiv::FaultPairing::pair(w, {{said(Kind::incomplete, 0, 200)}})
            .found == 0);
  CHECK(cursiv::FaultPairing::pair(w, {{said(Kind::incomplete, 0, 57)}})
            .found == 1);
  CHECK(cursiv::FaultPairing::pair(w, {{said(Kind::incomplete, 52, 200)}})
            .found == 1);
  CHECK(cursiv::FaultPairing::pair(w, {{said(Kind::deleted, 0, 200)}})
            .found == 1);
}

void a_case_maps_the_twin() {
  cursiv::RepairCase c;
  CHECK(cursiv::RepairCase::make(
      "abcdef", {{1, 2, "XY Z"}, {4, 0, "!"}}, c));
  CHECK(c.text == "aXY Zd!ef");
  CHECK(c.place(0, true) == 0);
  CHECK(c.place(1, true) == 1 && c.place(1, false) == 5);
  CHECK(c.place(3, true) == 5);
  CHECK(c.place(4, true) == 6 && c.place(4, false) == 7);
  CHECK(c.place(5, true) == 8);
  CHECK(c.placed(1) == 6);
  CHECK(!cursiv::RepairCase::make("abcdef", {{1, 2, ""}, {2, 1, ""}}, c));
  CHECK(!cursiv::RepairCase::make("abcdef", {{5, 3, ""}}, c));
}

// `p a q` with a stray `a ` written in front: the repair reads the stray one
// and skips the author's. A multiset would call the author's `a` kept.
void the_text_is_aligned_not_counted() {
  const std::string valid = "p a q";
  const std::vector<cursiv::CstLeaf> twin{leaf(0, 1), leaf(2, 3), leaf(4, 5)};
  cursiv::RepairCase c;
  CHECK(cursiv::RepairCase::make(valid, {{0, 0, "a "}}, c));
  const auto windows = cursiv::FaultWindow::of(twin, {}, c);
  CHECK(windows.size() == 1 && windows[0].coreFirst == windows[0].coreLast);
  const std::vector<cursiv::CstLeaf> got{leaf(0, 1), leaf(2, 3),
                                         leaf(4, 5, true), leaf(6, 7)};
  const std::vector<cursiv::ParseDiagnostic> d{said(Kind::deleted, 4, 5)};
  const auto none = [](std::size_t, cursiv::ElementId) { return false; };
  const auto t = cursiv::TextKept::of(twin, c, windows, got, d, none);
  CHECK(t.eligible == 3 && t.kept == 2 && t.deleted == 1);
  CHECK(t.abandoned == 0 && t.other == 0);
}

// `f ( x , y )` with its comma gone: an insertion of the comma restores it,
// an insertion of anything else invents; the neighbours stay the author's.
void an_insertion_that_restores_is_no_invention() {
  const std::string valid = "f ( x , y )";
  const std::vector<cursiv::CstLeaf> twin{leaf(0, 1), leaf(2, 3), leaf(4, 5),
                                          leaf(6, 7), leaf(8, 9), leaf(10, 11)};
  cursiv::RepairCase c;
  CHECK(cursiv::RepairCase::make(valid, {{6, 1, ""}}, c));
  const auto windows = cursiv::FaultWindow::of(twin, {}, c);
  CHECK(windows[0].coreFirst == 3 && windows[0].coreLast == 4);
  const std::vector<cursiv::CstLeaf> got{leaf(0, 1), leaf(2, 3), leaf(4, 5),
                                         leaf(6, 6, true), leaf(7, 8),
                                         leaf(9, 10)};
  constexpr cursiv::ElementId kComma = 7, kPlus = 8;
  const auto comma = [&](std::size_t token, cursiv::ElementId owed) {
    return owed == kComma && token == 3;
  };
  auto t = cursiv::TextKept::of(twin, c, windows, got,
                                {{said(Kind::inserted, 6, 6, kComma)}}, comma);
  CHECK(t.restored == 1 && t.invented == 0);
  CHECK(t.eligible == 5 && t.kept == 5);
  t = cursiv::TextKept::of(twin, c, windows, got,
                           {{said(Kind::inserted, 6, 6, kPlus)}}, comma);
  CHECK(t.restored == 0 && t.invented == 1);
  // The `x` dropped where the comma was owed: a value lost, not kept.
  const std::vector<cursiv::CstLeaf> lost{leaf(0, 1), leaf(2, 3),
                                          leaf(4, 5, true), leaf(7, 8),
                                          leaf(9, 10)};
  t = cursiv::TextKept::of(twin, c, windows, lost,
                           {{said(Kind::deleted, 4, 5)}}, comma);
  CHECK(t.kept == 4 && t.deleted == 1);
  // Under an abandonment the leaves stand, and are not kept.
  t = cursiv::TextKept::of(twin, c, windows, got,
                           {{said(Kind::incomplete, 2, 10)}}, comma);
  CHECK(t.kept == 1 && t.abandoned == 4);
}

std::unique_ptr<cursiv::Parser> dm_parser() {
  return dm::generated::DomainModel::create_parser();
}

const cursiv::fuzzer::CursivActions<dm::DomainModel> kDmActions{};

// A lost element costs its own subtree and nothing after it; a node a fault
// touches is out of the count on both sides.
void the_tree_is_aligned_per_member() {
  const auto parser = dm_parser();
  const auto reflection = dm::generated::DomainModel::create_ast_reflection();
  const std::string a =
      "package p { entity A {} entity B { x: A } entity C { y: A } }";
  const std::string b = "package p { entity A {} entity C { y: A } }";
  const auto ra = parser->parse(a);
  const auto rb = parser->parse(b);
  CHECK(ra.value != nullptr && rb.value != nullptr);
  if (ra.value == nullptr || rb.value == nullptr) return;
  const cursiv::AstOverlap overlap{*reflection, &kDmActions};
  const auto none = [](const cursiv::AstNode&) { return false; };
  const auto whole = overlap.run(*ra.value, ra.value, none);
  CHECK(whole.total == 7 && whole.kept == 7);
  const auto lost = overlap.run(*ra.value, rb.value, none);
  CHECK(lost.total == 7 && lost.kept == 5);
  const auto from = a.find("entity B");
  const auto to = a.find("entity C") - 1;
  const auto bEntity = [&](const cursiv::AstNode& n) {
    const auto& s = ra.cst()[n.cstNodeId];
    return s.begin >= from && s.end <= to;
  };
  const auto spared = overlap.run(*ra.value, rb.value, bEntity);
  CHECK(spared.total == 5 && spared.kept == 5);
  CHECK(overlap.run(*ra.value, nullptr, none).kept == 0);
  CHECK(overlap.same(*ra.value, *ra.value));
  CHECK(!overlap.same(*ra.value, *rb.value));
  // A package renamed in place is not kept; what it holds still is.
  const std::string c = "package a { entity X { x: T } }";
  const std::string d = "package b { entity X { x: T } }";
  const auto rc = parser->parse(c);
  const auto rd = parser->parse(d);
  const auto renamed = overlap.run(*rc.value, rd.value, none);
  CHECK(renamed.total == 4 && renamed.kept == 3);
}

// Closers owed after each token: the `}` of an entity opened before the cut,
// those of the packages around it -- never a record that no spelled token
// opened.
void a_cut_owes_its_open_closers() {
  const auto parser = dm_parser();
  const auto twin = cursiv::RepairTwin::read(
      *parser, "package p { package q { entity A { x: T } } datatype D }");
  CHECK(twin != nullptr);
  if (twin == nullptr) return;
  CHECK(twin->tokens.size() == 17);
  const std::vector<std::uint32_t> owed = cursiv::FaultCorpus::owed(*twin);
  const std::vector<std::uint32_t> want{1, 1, 1, 2, 2, 2, 3, 3, 3,
                                        3, 3, 3, 2, 1, 1, 1, 0};
  CHECK(owed == want);
}

}  // namespace

int main() {
  one_diagnostic_more_is_a_parasite();
  the_pairing_answers_the_most_faults();
  a_diagnostic_never_buys_back_a_parasite();
  a_wide_abandonment_answers_by_its_ends();
  a_case_maps_the_twin();
  the_text_is_aligned_not_counted();
  an_insertion_that_restores_is_no_invention();
  the_tree_is_aligned_per_member();
  a_cut_owes_its_open_closers();
  return cursiv::test::report("RepairMetric_test");
}
