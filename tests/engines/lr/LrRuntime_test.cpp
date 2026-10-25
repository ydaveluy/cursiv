// The runtime's own state: contexts, stacks, the roof, and records read
// through a narrow stamp.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/lr/runtime/LrCactus.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/lr/runtime/LrSearch.hpp"
#include "cursiv/engines/lr/runtime/LrStack.hpp"
#include "cursiv/engines/lr/runtime/LrStackClimb.hpp"
#include "cursiv/engines/lr/runtime/lr_lookahead.hpp"
#include "cursiv/engines/lr/runtime/lr_moves.hpp"
#include "cursiv/engines/lr/runtime/lr_repair_write.hpp"
#include "cursiv/engines/lr/runtime/lr_roof.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/shared/runtime/GapPositions.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/generated/Workspace/AstReflection.hpp"
#include "workspace/generated/Workspace/Module.hpp"
#include "workspace/generated/Workspace/Table.hpp"

// State shared by the generated driver, lookahead and recovery must retain
// the same meaning when a reading is paused or text is given up.
namespace runtime_state {

namespace {
using namespace cursiv;

constexpr StampLayout kStamp{};

static_assert(!std::is_copy_constructible_v<LrTape>);
static_assert(!std::is_move_constructible_v<LrTape>);
static_assert(!std::is_copy_constructible_v<LrContext>);
static_assert(!std::is_move_constructible_v<LrContext>);

// The positions one byte of `text` in `bytes` takes `in` to, as a generated
// gap program steps them.
template <class Set>
Set gap_byte(const std::string& text, std::string_view bytes, const Set& in) {
  Set out;
  in.each([&](TextOffset p) {
    if (p < text.size() && bytes.find(text[p]) != std::string_view::npos)
      out.set(p + 1);
  });
  return out;
}

void gap_repetitions_stop_when_no_later_round_can_change_the_answer() {
  constexpr std::uint32_t kMany = 0xfffffffeu;
  for (unsigned size : {0u, 1u, 63u, 64u, 65u, 80u, 127u, 128u, 129u}) {
    const std::string text(size, ' ');
    for (bool whole : {false, true}) {
      const auto spaces = [&](const auto& in) {
        return gap_repeat<kMany, kMany>(
            in, [&](const auto& cur) { return gap_byte(text, " ", cur); });
      };
      CHECK(whole ? !gap_matches<true>(text.data(), size, spaces)
                  : !gap_matches<false>(text.data(), size, spaces));
      // A nullable body reaches a fixed point without becoming empty.
      const auto optional = [&](const auto& in) {
        return gap_repeat<kMany, kMany>(in, [&](const auto& cur) {
          return gap_repeat<0u, 1u>(
              cur, [&](const auto& one) { return gap_byte(text, " ", one); });
        });
      };
      CHECK(whole ? gap_matches<true>(text.data(), size, optional)
                  : gap_matches<false>(text.data(), size, optional));
    }
  }
}

void gap_search_keeps_positions_at_word_boundaries() {
  for (unsigned size : {2u, 63u, 64u, 65u, 127u, 128u, 129u, 256u}) {
    for (unsigned at = 0; at < size; ++at) {
      std::string text(size, 'x');
      text[at] = 'a';
      if (at + 1 < size) text[at + 1] = 'b';
      const auto ab = [&](const auto& in) {
        return gap_byte(text, "b", gap_byte(text, "a", in));
      };
      CHECK(gap_matches<false>(text.data(), size, ab) ==
            (text.find("ab") != std::string::npos));
      CHECK(gap_matches<true>(text.data(), size, ab) == (text == "ab"));
    }
  }
}

void scratch_arrays_preserve_values_without_running_initializers() {
  // Scan has nonzero defaults, which must not overwrite existing candidates.
  const Scan saved[]{{13, 7}, {29, 11}};
  alignas(Scan) std::byte bytes[sizeof(saved)];
  std::memcpy(bytes, saved, sizeof(saved));
  Scan* scans = start_implicit_array<Scan>(bytes, 2);
  CHECK(scans[0].end == 13 && scans[0].term == 7);
  CHECK(scans[1].end == 29 && scans[1].term == 11);
  scans[1] = {31, 17};
  scans = start_implicit_array<Scan>(bytes, 2);
  CHECK(scans[1].end == 31 && scans[1].term == 17);
  CHECK(start_implicit_array<Scan>(nullptr, 0) == nullptr);

  // Origins may span several words and grow while active readings are held.
  Cst tree("");
  CstBuilder builder(tree);
  LrContext context("", builder);
  auto* origins = lr_origin_room<130>(context, 2);
  origins[0].set(129);
  origins[1].set(65);
  origins = lr_origin_room<130>(context, 128);
  CHECK(origins[0].test(129) && !origins[0].test(65));
  CHECK(origins[1].test(65) && !origins[1].test(129));
  origins[127] = {};
  origins[127].set(128);
  CHECK(origins[127].first() == 128);
}

struct RepairTable {
  StampLayout stamp{};
  struct Cfg {
    unsigned nnts = 3;
    unsigned nterms = 1;
    ElementId entryRow = 7;
    std::array<TermInfo, 1> terms{};
    std::array<LrProduction, 2> prods = [] {
      std::array<LrProduction, 2> p{};
      p[0].lhs = 1;
      p[0].len = 1;
      p[0].node = 7;
      p[1].lhs = 2;
      p[1].len = 1;
      return p;
    }();
  } cfg;
  struct Derived {
    std::array<LrReducePlan, 2> reductions{{
        {.form = LrReduceForm::foldClose}, {.form = LrReduceForm::through}}};
    std::array<LrReduceName, 0> reduceNames{};
  } derived;
  struct Cold {
    std::array<LrBitSet<1>, 4> follow{};
    std::array<std::uint8_t, 4> readsToken{1, 1, 0, 1};
  } cold;
  struct Acts {
    constexpr LrAction action(unsigned i) const {
      return LrAction{.kind =
                          i == 0 ? LrActionKind::reduce : LrActionKind::shift};
    }
  } acts;
};
constexpr RepairTable table{};

// A token reduces an empty rule through a guarded goto; if that goto refuses
// it, the state's fallback can supply an implied token instead.
struct EmptyGuardTable {
  struct Cfg {
    unsigned nterms = 3;
    unsigned nprods = 1;
    std::array<TermInfo, 3> terms = [] {
      std::array<TermInfo, 3> t{};
      t[2].laid = kLaidImplied;
      t[2].cond = 1;
      return t;
    }();
    std::array<LrProduction, 1> prods = [] {
      std::array<LrProduction, 1> p{};
      p[0].guarded = 1;
      return p;
    }();
  } cfg;
  struct Programs {
    std::array<std::uint16_t, 2> op{1, 8};  // reduce 0, shift 2
    constexpr unsigned cell(std::uint32_t, SymbolId term) const {
      return term == 1 ? 0 : term == 2 ? 1 : 2;
    }
    constexpr unsigned first(unsigned cell) const { return cell; }
    constexpr unsigned count(unsigned cell) const { return cell < 2 ? 1 : 0; }
  } programs;
  struct Gotos {
    constexpr std::uint32_t get(std::uint32_t, SymbolId) const { return 3; }
  } gotoPrograms;
  struct Cold {
    std::array<std::uint16_t, 3> soleReduce{kLrNoProd, kLrNoProd, kLrNoProd};
  } cold;
  std::array<std::uint16_t, 3> mode{};
  std::array<std::uint8_t, 1> modeTight{};
  std::array<std::uint32_t, 1> guardAt{};
  std::array<std::uint16_t, 4> guardOps{1, 0, kInvalidSymbol, 1};
  std::array<std::uint64_t, 1> held{};
  std::uint32_t maxCands = 0;
  constexpr bool mode_sub(std::uint16_t, std::uint16_t) const { return true; }
};
constexpr EmptyGuardTable emptyGuard{};

void a_refused_empty_goto_leaves_the_token_to_the_fallback() {
  for (const std::string_view text : {"+", "-", "?"}) {
    Cst tree(text);
    CstBuilder builder(tree);
    LrContext c(text, builder);
    c.stack.push(LrFrame{0, kNoElement, 0, 0, kLrNoWord});
    const LrDrive drive{
        .cond = +[](const LrContext& c, unsigned k, TextOffset, const Token&) {
          return k == 0 ? c.src[0] == '+' : c.src[0] != '?';
        }};
    c.drive = &drive;
    LrShadow s;
    s.run.stand_on(c.stack);
    s.la = Token{0, 1, 1};
    s.laMode = 0;
    const auto actions = lr_read_actions<emptyGuard>(
        c, [&]() -> LrShadow& { return s; }, [] {}, [] {});
    CHECK(s.run.size() == 1 && s.run.back() == 0);
    if (text == "+") {
      CHECK(actions.count == 1 && actions.first == 0);
      CHECK(s.la.sym == 1 && s.virt.pending == 0);
    } else if (text == "-") {
      CHECK(actions.count == 1 && actions.first == 1);
      CHECK(s.la.sym == 2 && s.virt.pending == 2);
    } else {
      CHECK(actions.count == 0 && actions.lone == kLrNoProd);
      CHECK(s.la.sym == 1 && s.virt.pending == 0);
    }
  }
}

void a_lookahead_keeps_its_lexical_mode() {
  LrShadow a, b;
  a.la = b.la = Token{0, 1, 1};
  a.laMode = 0;
  b.laMode = 1;
  const auto same_class = [](std::uint32_t s) { return s; };
  CHECK(!lr_shadow_same(a, b, same_class));
  b.laMode = 0;
  CHECK(lr_shadow_same(a, b, same_class));
}

// Two runs are one where the tops agree and the frames below are of one class:
// only the top is read as the state it is.
void a_class_stands_for_the_frames_below_the_top() {
  LrStack base;
  base.push(LrFrame{1, kNoElement, 0, 0, kLrNoWord});
  LrStackClimb a, b, c;
  a.stand_on(base);
  b.stand_on(base);
  c.stand_on(base);
  for (const std::uint32_t s : {5u, 8u}) a.push(s);
  for (const std::uint32_t s : {6u, 8u}) b.push(s);
  for (const std::uint32_t s : {5u, 9u}) c.push(s);
  const auto own = [](std::uint32_t s) { return s; };
  const auto merged = [](std::uint32_t s) { return s == 6 ? 5u : s; };
  CHECK(!lr_same_run(a, b, own));
  CHECK(lr_same_run(a, b, merged));
  CHECK(!lr_same_run(a, c, merged));
  b.pop(1);
  CHECK(!lr_same_run(a, b, merged));
}

void abandoning_a_suffix_discards_only_its_pending_close() {
  for (const ElementId row : {kNoRow, ElementId{7}}) {
    for (const auto pending : {0u, 2u, kLrNoPending}) {
      Cst tree("ab");
      CstBuilder builder(tree);
      LrContext c("ab", builder);
      c.tape.leaf<kStamp>(0, 1, 8, 1);
      c.tape.leaf<kStamp>(1, 2, kNoRow, 2);
      c.stack.push(LrFrame{0, kNoRow, 0, 0, kLrNoWord});
      c.stack.push(LrFrame{1, 8, 0, 0, 0});
      c.stack.push(LrFrame{2, 7, 1, 1, 1});
      c.committed = 2;
      c.tape.pending_close(pending);
      lr_write_abandon<table>(c, row == kNoRow ? 2 : 1, row);
      CHECK(c.tape.pending_close() ==
            (row == kNoRow && pending == 0 ? 0 : kLrNoPending));
    }
  }
}

// A recovered region naming no row owes the tree nothing: the record before
// it still ends the derivation.
void adjacency_skips_a_recovered_region_without_rows() {
  Cst tree("ab");
  CstBuilder builder(tree);
  builder.stream().leaf(0, 1, kStamp.make(3, 1));
  builder.stream().leaf(1, 2, kStamp.make(kNoRow, 2));
  builder.stream().repaired_close(1, 2, 2, kStamp.make(kNoRow, 0));
  LrContext c("ab", builder);
  CHECK(lr_last_real_word(c, 0));
}

}  // namespace

int run() {
  gap_repetitions_stop_when_no_later_round_can_change_the_answer();
  gap_search_keeps_positions_at_word_boundaries();
  scratch_arrays_preserve_values_without_running_initializers();
  a_refused_empty_goto_leaves_the_token_to_the_fallback();
  a_lookahead_keeps_its_lexical_mode();
  a_class_stands_for_the_frames_below_the_top();
  abandoning_a_suffix_discards_only_its_pending_close();
  adjacency_skips_a_recovered_region_without_rows();
  return 0;
}

}  // namespace runtime_state

namespace stack {

int run() {
  for (const unsigned capacity : {1u, 64u, 128u}) {
    cursiv::LrStack alias;
    alias.reserve(capacity);
    for (unsigned i = 0; i < capacity; ++i) alias.push({1, 2, 3, 4, i});
    alias.push(alias[0]);
    CHECK(alias.size() == capacity + 1);
    CHECK(alias.top().state == 1 && alias.top().row == 2);
    CHECK(alias.top().open == 3 && alias.top().begin == 4);
    CHECK(alias.top().word == 0);
    alias.push(alias[capacity - 1]);
    CHECK(alias.top().word == capacity - 1);
    cursiv::LrStack moved = std::move(alias);
    CHECK(alias.size() == 0 && moved.size() == capacity + 2);
    alias.push(moved.top());
    CHECK(alias.top().word == capacity - 1);
  }
  cursiv::LrStack stack;
  stack.reserve(1);
  stack.push({0, 1, 2, 3, 4});
  for (std::uint32_t i = 1; i != 257; ++i) {
    stack.reduce(0);
    CHECK(stack.size() == i + 1);
    CHECK(stack[i - 1].word == (i == 1 ? 4 : i - 1));
    stack.top() = {static_cast<std::uint16_t>(i), 5, i + 1, i + 2, i};
  }
  const auto* first = &stack[0];
  stack.reduce(1);
  CHECK(stack.size() == 257);
  CHECK(stack.top().state == 256);
  stack.top() = {17, 18, 19, 20, 21};
  stack.reduce(256);
  CHECK(stack.size() == 2);
  CHECK(&stack[0] == first);
  CHECK(stack[0].state == 0 && stack[0].word == 4);
  stack.top() = {22, 23, 24, 25, 26};
  cursiv::LrStack copy = stack;
  CHECK(copy.size() == 2);
  CHECK(copy.top().state == 22 && copy.top().word == 26);
  CHECK(stack[0].open == 2 && stack[0].begin == 3);
  return 0;
}

}  // namespace stack

// The close an acceptance owes when a record stands beside the root. The run
// is laid by hand: what decides is the shape of the records, not a grammar,
// so the four shapes are spelled here rather than drawn.
namespace roof {

namespace {

using cursiv::CstRun;
using cursiv::kNoElement;
using cursiv::NodeId;

constexpr cursiv::StampLayout kStamp{};
constexpr cursiv::ElementId kEntry = 7, kRow = 3;
const std::uint32_t kBare = kStamp.make(kNoElement, 0);
const std::uint32_t kNamed = kStamp.make(kRow, 0);

// Whether the roof laid its close, and that what it laid spans the whole run.
bool roofed(CstRun& run, NodeId root, const char* what) {
  const cursiv::NodeCount was = run.records();
  cursiv::lr_roof_forest<kStamp>(run, root, kEntry);
  if (run.records() == was) return false;
  const cursiv::CstNode& lid = run.at(run.records() - 1);
  if (!lid.recovered || lid.begin != run.at(0).begin ||
      lid.end != run.at(was - 1).end || lid.subtreeSize != was + 1) {
    std::printf("FAIL %s: the close does not span the run\n", what);
    ++failures;
  }
  return true;
}

// A leaf naming a row is a node the tree owes and nothing above will take:
// the acceptance closes over it. It is the ONLY leaf that is not ordinary.
void a_leaf_naming_a_row_is_roofed() {
  CstRun run;
  run.reserve_for(64);
  run.leaf(0, 1, kBare);
  run.close(0, 1, 2, kBare);  // the root
  run.leaf(1, 2, kNamed);
  CHECK(roofed(run, 1, "a leaf naming a row after the root"));

  // ...and the walk before the root is the same test.
  CstRun before;
  before.reserve_for(64);
  before.leaf(0, 1, kNamed);
  before.leaf(1, 2, kBare);
  before.close(1, 2, 2, kBare);  // the root, spanning the second leaf alone
  CHECK(roofed(before, 2, "a leaf naming a row before the root"));
}

// Trivia, a repair's discards, a token the entry never spanned: a leaf that
// names no row is described by nothing and owed nothing.
void a_bare_leaf_is_left_alone() {
  CstRun run;
  run.reserve_for(64);
  run.leaf(0, 1, kBare);
  run.close(0, 1, 2, kBare);
  run.leaf(1, 2, kBare);
  run.hidden(2, 3, kNamed);  // trivia stays trivia, whatever it stamps
  CHECK(!roofed(run, 1, "a bare leaf after the root"));
}

// A composite beside the root is a subtree the document keeps and the tree
// does not describe, named or not.
void a_composite_beside_the_root_is_roofed() {
  CstRun run;
  run.reserve_for(64);
  run.leaf(0, 1, kBare);
  run.close(0, 1, 2, kBare);
  run.leaf(1, 2, kBare);
  run.close(1, 2, 2, kBare);
  CHECK(roofed(run, 1, "a composite after the root"));
}

// A root that spans the whole run owes nothing.
void a_whole_document_is_left_alone() {
  CstRun run;
  run.reserve_for(64);
  run.leaf(0, 1, kNamed);
  run.leaf(1, 2, kNamed);
  run.close(0, 2, 3, kBare);
  CHECK(!roofed(run, 2, "a root over the whole run"));
}

// A root that names the document is the one node over it: its own record
// takes what stands before it, and one it shares is roofed with no second.
void a_root_naming_the_document_stays_its_one_node() {
  CstRun own;
  own.reserve_for(64);
  own.leaf(0, 1, kBare);
  own.close(0, 1, 2, kBare);
  own.leaf(1, 2, kBare);
  own.close(1, 2, 2, kStamp.make(kEntry, 0));
  CHECK(!roofed(own, 3, "a root naming the document"));
  const cursiv::CstNode& root = own.at(3);
  CHECK(root.begin == 0 && root.subtreeSize == 4 && root.recovered);

  CstRun shared;
  shared.reserve_for(64);
  shared.leaf(0, 1, kBare);
  shared.close(0, 1, 2, kBare);
  shared.leaf(1, 2, kNamed);
  CHECK(roofed(shared, 2, "a root sharing the document's record"));
  CHECK(kStamp.row(shared.at(3)) == kNoElement);
}

}  // namespace

int run() {
  a_leaf_naming_a_row_is_roofed();
  a_bare_leaf_is_left_alone();
  a_composite_beside_the_root_is_roofed();
  a_whole_document_is_left_alone();
  a_root_naming_the_document_stays_its_one_node();
  return 0;
}

}  // namespace roof

// The workspace fixture generated with its stamp's row forced narrow: the
// parse, the repair, the tree and the caret read every record in the table's
// layout, where the full row's would take the state's bits for the row's.
namespace stamp_narrow {

namespace {

namespace g = ws::generated::Workspace;

static_assert(g::kTable.stamp.rowBits == CURSIV_NARROW_ROW_BITS);

const cursiv::Parser& service() {
  static const std::unique_ptr<cursiv::Parser> engine = g::create_parser();
  return *engine;
}

// Every record names a row of the table or none, and a state of the table;
// the count is of the records the full row would misread.
unsigned misread_by_the_full_row(const cursiv::Cst& cst) {
  constexpr cursiv::StampLayout L = g::kTable.stamp;
  unsigned misread = 0;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    const cursiv::ElementId row = L.row(n);
    CHECK(row == cursiv::kNoElement || row < g::kAstActions.entryCount);
    CHECK(L.state(n) < g::kTable.nstates);
    if (cursiv::StampLayout{}.row(n) != row) ++misread;
  }
  return misread;
}

void a_clean_document_builds_its_tree() {
  const std::string text =
      "// a comment the tree records\n"
      "datatype String\n"
      "package p { entity A extends B { many x : String /* doc */ } }\n"
      "entity B { y : p.A }\n";
  const cursiv::ParseResult r = service().parse(text);
  CHECK(r.accepted && r.diagnostics.empty() && r.value != nullptr);
  CHECK(misread_by_the_full_row(r.cst()) > 0);
  const cursiv::Grammar& grammar = service().getGrammar();
  bool named = false;
  for (cursiv::NodeId i = 0; i < r.cst().size(); ++i)
    named = named || !grammar.recordFeature(r.cst()[i]).empty();
  CHECK(named);
}

void a_broken_document_is_repaired_into_a_tree() {
  const std::string text = "entity A { x : }\nentity B { y z : A\n";
  const cursiv::ParseResult r = service().parse(text);
  CHECK(r.accepted && !r.diagnostics.empty() && r.value != nullptr);
  misread_by_the_full_row(r.cst());
}

bool offers(const std::vector<cursiv::NextFeature>& out,
            std::string_view spelled) {
  for (const cursiv::NextFeature& f : out)
    if (service().getGrammar().terminalText(f.feature) == spelled) return true;
  return false;
}

void the_caret_reads_the_states_the_leaves_carry() {
  const std::string text = "entity A { x : String ";
  const cursiv::ParseResult r = service().parse(text);
  std::vector<cursiv::NextFeature> out;
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  service().nextFeaturesAt(r.cst(), at, at, out);
  CHECK(offers(out, "}") && offers(out, "."));
}

}  // namespace

int run() {
  a_clean_document_builds_its_tree();
  a_broken_document_is_repaired_into_a_tree();
  the_caret_reads_the_states_the_leaves_carry();
  return 0;
}

}  // namespace stamp_narrow

// A stack the cactus laid for an attempt is given back whole: the nodes it
// interned leave their parents, whichever order the children stood in, and
// what stood before answers as it did.
namespace cactus_rewind {

int run() {
  cursiv::LrCactus cactus;
  const std::uint32_t root = cactus.intern(1, cursiv::kLrNoNode);
  const std::uint32_t a = cactus.intern(2, root);
  const std::uint32_t b = cactus.intern(3, root);
  CHECK(cactus.intern(2, root) == a);
  const auto mark = cactus.mark();
  const std::uint32_t c = cactus.intern(4, root);
  const std::uint32_t d = cactus.intern(5, c);
  // A hit moves its node to the front of its siblings.
  CHECK(cactus.intern(3, root) == b && cactus.intern(2, root) == a);
  const std::uint32_t other = cactus.intern(9, cursiv::kLrNoNode);
  cactus.begin_push(7, cursiv::kLrNoNode);
  CHECK(c > b && d > c && other > d);
  cactus.rewind(mark);
  CHECK(cactus.intern(2, root) == a && cactus.intern(3, root) == b);
  // The ids that were given up are laid again, and the roots forget theirs.
  CHECK(cactus.intern(4, root) == c);
  CHECK(cactus.intern(9, cursiv::kLrNoNode) == d);
  CHECK(cactus.depth(a) == 2 && cactus.parent(b) == root);
  return 0;
}

}  // namespace cactus_rewind

int main() {
  int status = 0;
  status |= cactus_rewind::run();
  status |= runtime_state::run();
  status |= stack::run();
  status |= roof::run();
  status |= stamp_narrow::run();
  return cursiv::test::report("LrRuntime_test") | status;
}
