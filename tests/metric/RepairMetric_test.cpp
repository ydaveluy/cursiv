// The recovery metric on cases built by hand: the pairing of faults and
// diagnostics, the alignment of the author's text, and the overlap of two
// trees. What a parser answers is not judged here.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/CursivActions.hpp"
#include "cursiv/SeededSource.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/shared/runtime/RepairFault.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "domainmodel/DomainModelGrammar.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "recovery_metric/support/AstOverlap.hpp"
#include "recovery_metric/support/FaultCorpus.hpp"
#include "recovery_metric/support/FaultPairing.hpp"
#include "recovery_metric/support/FaultWindow.hpp"
#include "recovery_metric/support/RepairCase.hpp"
#include "recovery_metric/support/RepairFault.hpp"
#include "recovery_metric/support/RepairTally.hpp"
#include "recovery_metric/support/RepairTwin.hpp"
#include "recovery_metric/support/TextKept.hpp"

using cursiv::test::failures;

namespace metric = cursiv::recovery_metric;

static_assert(
    !std::is_same_v<cursiv::engine::RepairFault, metric::RepairFault>);

namespace {

metric::FaultWindow window(cursiv::TextOffset lo, cursiv::TextOffset hi,
                           cursiv::TextOffset at) {
  metric::FaultWindow w;
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

void one_diagnostic_more_is_spurious() {
  const std::vector<metric::FaultWindow> w{window(10, 20, 15)};
  std::vector<cursiv::ParseDiagnostic> d{said(Kind::Inserted, 15, 15)};
  auto p = metric::FaultPairing::pair(w, d);
  CHECK(p.found == 1 && p.missed == 0 && p.spurious == 0);
  d.push_back(said(Kind::Deleted, 12, 13));
  p = metric::FaultPairing::pair(w, d);
  CHECK(p.found == 1 && p.spurious == 1);
  d.push_back(said(Kind::Deleted, 100, 101));
  p = metric::FaultPairing::pair(w, d);
  CHECK(p.found == 1 && p.spurious == 2);
}

// The nearest diagnostic goes first, and gives way when another fault has
// nothing else within reach.
void the_pairing_answers_the_most_faults() {
  const std::vector<metric::FaultWindow> w{window(10, 20, 14),
                                           window(12, 22, 21)};
  const std::vector<cursiv::ParseDiagnostic> d{said(Kind::Inserted, 14, 14),
                                               said(Kind::Inserted, 19, 19)};
  const auto p = metric::FaultPairing::pair(w, d);
  CHECK(p.found == 2 && p.spurious == 0);
  CHECK(p.answer[0] == 0 && p.answer[1] == 1);
  const std::vector<metric::FaultWindow> tight{window(10, 20, 14),
                                               window(14, 14, 14)};
  const auto q = metric::FaultPairing::pair(tight, d);
  CHECK(q.found == 2 && q.answer[1] == 0 && q.answer[0] == 1);
}

// Over drawn windows and diagnostics: adding one never lowers what was found
// nor what was left over.
void a_diagnostic_never_buys_back_a_spurious_one() {
  cursiv::fuzzer::SeededSource rng(0x9a17);
  for (int round = 0; round < 400; ++round) {
    std::vector<metric::FaultWindow> w;
    const unsigned faults = 1 + static_cast<unsigned>(rng.next() % 5);
    for (unsigned f = 0; f < faults; ++f) {
      const auto lo = static_cast<cursiv::TextOffset>(rng.next() % 100);
      const auto hi = lo + static_cast<cursiv::TextOffset>(rng.next() % 12);
      w.push_back(window(lo, hi, lo + (hi - lo) / 2));
    }
    std::vector<cursiv::ParseDiagnostic> d;
    auto before = metric::FaultPairing::pair(w, d);
    for (int k = 0; k < 8; ++k) {
      const auto b = static_cast<cursiv::TextOffset>(rng.next() % 110);
      const auto kind = static_cast<Kind>(rng.next() % 4);
      d.push_back(said(kind, b, b + static_cast<cursiv::TextOffset>(rng.next() % 30)));
      const auto after = metric::FaultPairing::pair(w, d);
      CHECK(after.found >= before.found);
      CHECK(after.spurious >= before.spurious);
      CHECK(after.found + after.spurious == d.size());
      before = after;
    }
  }
}

// An abandonment reaches a fault at one of its two ends, never through the
// text it covers.
void a_wide_abandonment_answers_by_its_ends() {
  const std::vector<metric::FaultWindow> w{window(50, 60, 55)};
  CHECK(
      metric::FaultPairing::pair(w, {{said(Kind::Incomplete, 0, 200)}}).found ==
      0);
  CHECK(
      metric::FaultPairing::pair(w, {{said(Kind::Incomplete, 0, 57)}}).found ==
      1);
  CHECK(metric::FaultPairing::pair(w, {{said(Kind::Incomplete, 52, 200)}})
            .found == 1);
  CHECK(metric::FaultPairing::pair(w, {{said(Kind::Deleted, 0, 200)}}).found ==
        1);
}

void a_case_maps_the_twin() {
  metric::RepairCase c;
  CHECK(metric::RepairCase::make(
      "abcdef", {{1, 2, "XY Z"}, {4, 0, "!"}}, c));
  CHECK(c.text == "aXY Zd!ef");
  CHECK(c.place(0, true) == 0);
  CHECK(c.place(1, true) == 1 && c.place(1, false) == 5);
  CHECK(c.place(3, true) == 5);
  CHECK(c.place(4, true) == 6 && c.place(4, false) == 7);
  CHECK(c.place(5, true) == 8);
  CHECK(c.placed(1) == 6);
  CHECK(!metric::RepairCase::make("abcdef", {{1, 2, ""}, {2, 1, ""}}, c));
  CHECK(!metric::RepairCase::make("abcdef", {{5, 3, ""}}, c));
}

// `p a q` with an extra `a ` written in front: the repair reads the extra one
// and skips the author's. A multiset would call the author's `a` kept.
void the_text_is_aligned_not_counted() {
  const std::string valid = "p a q";
  const std::vector<cursiv::CstLeaf> twin{leaf(0, 1), leaf(2, 3), leaf(4, 5)};
  metric::RepairCase c;
  CHECK(metric::RepairCase::make(valid, {{0, 0, "a "}}, c));
  const auto windows = metric::FaultWindow::of(twin, {}, c);
  CHECK(windows.size() == 1 && windows[0].coreFirst == windows[0].coreLast);
  const std::vector<cursiv::CstLeaf> got{leaf(0, 1), leaf(2, 3),
                                         leaf(4, 5, true), leaf(6, 7)};
  const std::vector<cursiv::ParseDiagnostic> d{said(Kind::Deleted, 4, 5)};
  const auto none = [](std::size_t, cursiv::ElementId) { return false; };
  const auto t = metric::TextKept::of(twin, c, windows, got, d, none);
  CHECK(t.eligible == 3 && t.kept == 2 && t.deleted == 1);
  CHECK(t.abandoned == 0 && t.other == 0);
}

// `f ( x , y )` with its comma gone: an insertion of the comma restores it,
// an insertion of anything else invents; the neighbours stay the author's.
void an_insertion_that_restores_is_no_invention() {
  const std::string valid = "f ( x , y )";
  const std::vector<cursiv::CstLeaf> twin{leaf(0, 1), leaf(2, 3), leaf(4, 5),
                                          leaf(6, 7), leaf(8, 9), leaf(10, 11)};
  metric::RepairCase c;
  CHECK(metric::RepairCase::make(valid, {{6, 1, ""}}, c));
  const auto windows = metric::FaultWindow::of(twin, {}, c);
  CHECK(windows[0].coreFirst == 3 && windows[0].coreLast == 4);
  const std::vector<cursiv::CstLeaf> got{leaf(0, 1), leaf(2, 3), leaf(4, 5),
                                         leaf(6, 6, true), leaf(7, 8),
                                         leaf(9, 10)};
  constexpr cursiv::ElementId kComma = 7, kPlus = 8;
  const auto comma = [&](std::size_t token, cursiv::ElementId expected) {
    return expected == kComma && token == 3;
  };
  auto t = metric::TextKept::of(twin, c, windows, got,
                                {{said(Kind::Inserted, 6, 6, kComma)}}, comma);
  CHECK(t.restored == 1 && t.invented == 0);
  CHECK(t.eligible == 5 && t.kept == 5);
  t = metric::TextKept::of(twin, c, windows, got,
                           {{said(Kind::Inserted, 6, 6, kPlus)}}, comma);
  CHECK(t.restored == 0 && t.invented == 1);
  // The `x` dropped where the comma was missing: a value lost, not kept.
  const std::vector<cursiv::CstLeaf> lost{leaf(0, 1), leaf(2, 3),
                                          leaf(4, 5, true), leaf(7, 8),
                                          leaf(9, 10)};
  t = metric::TextKept::of(twin, c, windows, lost,
                           {{said(Kind::Deleted, 4, 5)}}, comma);
  CHECK(t.kept == 4 && t.deleted == 1);
  // Under an abandonment the leaves stand, and are not kept.
  t = metric::TextKept::of(twin, c, windows, got,
                           {{said(Kind::Incomplete, 2, 10)}}, comma);
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
  const metric::AstOverlap overlap{*reflection, kDmActions};
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

// The domain model's actions, with the values of some nodes said otherwise:
// text holding any byte, or a value no reader reaches.
struct SaidValues final : cursiv::fuzzer::GrammarActions {
  struct Value {
    std::string property;
    cursiv::fuzzer::ValueKind kind;
    std::string text;
  };
  std::unordered_map<const cursiv::AstNode*, std::vector<Value>> values;

  cursiv::AstNode* make(cursiv::ElementId e,
                        cursiv::fuzzer::WalkTree& t) const override {
    return kDmActions.make(e, t);
  }
  cursiv::ElementId primary(cursiv::ElementId e) const override {
    return kDmActions.primary(e);
  }
  std::span<const cursiv::fuzzer::InfixOp> operators(
      cursiv::ElementId e) const override {
    return kDmActions.operators(e);
  }
  cursiv::fuzzer::Write fold(cursiv::ElementId e, cursiv::ElementId op,
                             cursiv::AstNode& node, cursiv::AstNode* left,
                             std::string_view text,
                             cursiv::AstNode* right) const override {
    return kDmActions.fold(e, op, node, left, text, right);
  }
  cursiv::fuzzer::Write writeNode(cursiv::ElementId e, cursiv::AstNode& owner,
                                  cursiv::AstNode* child) const override {
    return kDmActions.writeNode(e, owner, child);
  }
  cursiv::fuzzer::Write writeText(cursiv::ElementId e,
                                  cursiv::ElementId payload,
                                  cursiv::AstNode& owner,
                                  std::string_view text) const override {
    return kDmActions.writeText(e, payload, owner, text);
  }
  std::optional<std::string> denote(cursiv::ElementId e,
                                    std::string_view text) const override {
    return kDmActions.denote(e, text);
  }
  void visitValues(
      const cursiv::AstNode& n,
      cursiv::function_ref<void(std::string_view, std::uint32_t,
                                cursiv::fuzzer::ValueKind, std::string_view)>
          yield) const override {
    const auto it = values.find(&n);
    if (it == values.end()) return kDmActions.visitValues(n, yield);
    for (const Value& v : it->second) yield(v.property, 0, v.kind, v.text);
  }
};

// A node is kept only when every value of it reads the same: text moved from
// one member into the next is a change, and a value no reader reaches leaves
// the node out of the count and the tree not whole.
void a_node_is_kept_by_values_read_whole() {
  const auto parser = dm_parser();
  const auto reflection = dm::generated::DomainModel::create_ast_reflection();
  const std::string text = "package p { entity A {} }";
  const auto ra = parser->parse(text);
  const auto rb = parser->parse(text);
  CHECK(ra.value != nullptr && rb.value != nullptr);
  if (ra.value == nullptr || rb.value == nullptr) return;
  const auto entity =
      [](const cursiv::AstNode* root) -> const cursiv::AstNode* {
    const auto* model = cursiv::ast_cast<const dm::ast::DomainModel>(root);
    const auto* package =
        cursiv::ast_cast<const dm::ast::PackageDeclaration>(model->elements[0]);
    return package->elements[0];
  };
  const auto none = [](const cursiv::AstNode&) { return false; };
  using K = cursiv::fuzzer::ValueKind;

  SaidValues moved;
  moved.values[entity(ra.value)] = {{"name", K::Text, "x\037b#0=y"},
                                    {"b", K::Text, ""}};
  moved.values[entity(rb.value)] = {{"name", K::Text, "x"},
                                    {"b", K::Text, "y\037b#0="}};
  const metric::AstOverlap byMoved{*reflection, moved};
  CHECK(!byMoved.same(*ra.value, *rb.value));
  const auto m = byMoved.run(*ra.value, rb.value, none);
  CHECK(m.total == 3 && m.kept == 2);

  SaidValues unread;
  unread.values[entity(ra.value)] = {{"name", K::Unreadable, ""}};
  unread.values[entity(rb.value)] = {{"name", K::Unreadable, ""}};
  const metric::AstOverlap byUnread{*reflection, unread};
  CHECK(!byUnread.same(*ra.value, *rb.value));
  metric::RepairVerdict v;
  v.ast = byUnread.run(*ra.value, rb.value, none);
  CHECK(v.ast.total == 2 && v.ast.kept == 2 && v.ast.unread == 1);
  metric::RepairTally tally;
  tally.add(v);
  CHECK(tally.astWhole == 0);
}

// The closers still due after each token: the `}` of an entity opened before
// the cut, those of the packages around it, never a record that no written
// token opened.
void a_cut_counts_the_closers_still_due() {
  const auto parser = dm_parser();
  const auto twin = metric::RepairTwin::read(
      *parser, "package p { package q { entity A { x: T } } datatype D }");
  CHECK(twin != nullptr);
  if (twin == nullptr) return;
  CHECK(twin->tokens.size() == 17);
  const std::vector<std::uint32_t> missing =
      metric::FaultCorpus::missing_closers(*twin);
  const std::vector<std::uint32_t> want{1, 1, 1, 2, 2, 2, 3, 3, 3,
                                        3, 3, 3, 2, 1, 1, 1, 0};
  CHECK(missing == want);
}

}  // namespace

int main() {
  one_diagnostic_more_is_spurious();
  the_pairing_answers_the_most_faults();
  a_diagnostic_never_buys_back_a_spurious_one();
  a_wide_abandonment_answers_by_its_ends();
  a_case_maps_the_twin();
  the_text_is_aligned_not_counted();
  an_insertion_that_restores_is_no_invention();
  the_tree_is_aligned_per_member();
  a_node_is_kept_by_values_read_whole();
  a_cut_counts_the_closers_still_due();
  return cursiv::test::report("RepairMetric_test");
}
