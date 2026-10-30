// The construction, piece by piece: action and close rows, programs, ordered
// reduces, the stamp split and the lowering's limits.

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/engines/lr/gen/LrFinalTable.hpp"
#include "cursiv/engines/lr/gen/LrFirstSets.hpp"
#include "cursiv/engines/lr/gen/LrLowered.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/gen/LrProgramLayout.hpp"
#include "cursiv/engines/lr/gen/LrProgramMatrix.hpp"
#include "cursiv/engines/lr/gen/lr_build.hpp"
#include "cursiv/engines/lr/gen/lr_build_derived.hpp"
#include "cursiv/engines/lr/gen/lr_build_elect.hpp"
#include "cursiv/engines/lr/gen/lr_build_fold.hpp"
#include "cursiv/engines/lr/gen/lr_build_passes.hpp"
#include "cursiv/engines/lr/gen/lr_emit_parser.hpp"
#include "cursiv/engines/lr/gen/lr_emit_table.hpp"
#include "cursiv/engines/lr/gen/lr_entry_rows.hpp"
#include "cursiv/engines/lr/gen/lr_intern_name.hpp"
#include "cursiv/engines/lr/gen/lr_lower_epsilon.hpp"
#include "cursiv/engines/lr/gen/lr_lower_settle.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/lr_moves.hpp"
#include "cursiv/engines/lr/table/LrProgramRows.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/set_dedup.hpp"
#include "cursiv/engines/shared/gen/stamp_split.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/test/Check.hpp"

namespace lr_construction_cases {

namespace {

// A production of `lhs` over `syms`, laid at the end of the build's rows.
void lay(cursiv::LrBuildCfg& g, std::uint16_t lhs,
         const std::vector<std::uint16_t>& syms) {
  const auto p = g.nprods++;
  g.prods[p] = cursiv::LrLowProd{};
  g.prods[p].lhs = lhs;
  g.prods[p].at = g.nslots;
  g.prods[p].len = static_cast<std::uint16_t>(syms.size());
  g.prods[p].node = cursiv::kNoRow;
  g.prodBuild[p] = cursiv::LrProdBuild{};
  g.prodBuild[p].owner = cursiv::kNoRow;
  for (const std::uint16_t x : syms) {
    g.slots[g.nslots] = cursiv::LrLowSlot{};
    g.slots[g.nslots].row = cursiv::kNoRow;
    g.slotCfg[g.nslots] = 0;
    g.slotSym[g.nslots++] = x;
  }
}

}  // namespace

namespace action_rows {

namespace {

void stable_set_classes() {
  using namespace cursiv;
  const auto check = [](const std::vector<DynSet>& rows) {
    std::map<std::vector<std::uint64_t>, unsigned> classes;
    std::vector<std::uint16_t> expected, actual(rows.size());
    for (const auto& row : rows)
      expected.push_back(static_cast<std::uint16_t>(
          classes.try_emplace(row.w, classes.size()).first->second));
    const auto n = static_cast<unsigned>(rows.size());
    CHECK(set_dedup(rows.data(), n, actual.data()) == classes.size());
    CHECK(actual == expected);
    CHECK(set_dedup(rows.data(), n, nullptr) == classes.size());
  };
  check({});
  check(std::vector<DynSet>(100));
  std::vector<DynSet> rows;
  for (unsigned i = 0; i < 8192; ++i) {
    const unsigned key = (i * 2053u) % 4096u;
    DynSet row;
    row.set(key);
    rows.push_back(row);
    if (i % 3 == 0) rows.emplace_back();
  }
  check(rows);

  // Different values in one hash bucket must still have distinct IDs, while
  // later occurrences must find their original class through the collisions.
  ChainIndex index;
  index.init(128);
  rows.clear();
  for (std::uint64_t word = 1; rows.size() < 64; ++word) {
    DynSet row{{word}};
    if (((set_hash(row) >> 24) & index.mask) == 0)
      rows.push_back(std::move(row));
  }
  for (unsigned i = 64; i > 0; --i) rows.push_back(rows[i - 1]);
  check(rows);
}

void deep_entry_chains() {
  using namespace cursiv;
  LrBuildTable a;
  a.writes.nchains = 3;
  a.writes.chains.v = {{0, 1}, {1, 2}, {3, 1}};
  a.writes.acts.v = {7, 11, 12, 13};
  a.writes.chainBuild.v.resize(3);
  constexpr unsigned depth = 60000;
  for (unsigned i = 0; i < depth; ++i)
    a.fused.v.push_back({static_cast<std::uint16_t>(i == 0 ? 0 : i + 2),
                         static_cast<std::uint16_t>(1 + i % 2)});
  a.nfused = depth;
  const auto entry = static_cast<std::uint16_t>(depth + 2);
  std::vector<std::uint16_t> held, over;
  const AstEntryTable table = lr_entry_table(a);
  table.rows(entry, held, over);
  std::vector<std::uint16_t> expected{7};
  for (unsigned i = 0; i + 1 < depth; ++i) {
    if (i % 2 == 0)
      expected.insert(expected.end(), {11, 12});
    else
      expected.push_back(13);
  }
  CHECK(held == expected);
  CHECK(over == std::vector<std::uint16_t>{13});
  table.rows(0, held, over);
  CHECK(held == std::vector<std::uint16_t>{7});
  CHECK(over.empty());
}

}  // namespace

void run() {
  stable_set_classes();
  deep_entry_chains();
  using namespace cursiv;
  const LrAction shift{.term = 2, .arg = 7, .site = 9, .outer = 11};
  const LrAction reduce{.term = 2, .kind = LrActionKind::reduce, .arg = 5};
  std::vector<std::vector<LrAction>> rows{
      {}, {shift, reduce}, {shift, reduce}, {shift}, {shift}, {shift},
      {shift}, {shift}, {reduce, shift}, {reduce}, {}};
  rows[4][0].term = 4;
  rows[5][0].site = 10;
  rows[6][0].outer = 12;
  rows[7][0].arg = 8;
  LrBuildTable a;
  a.nstates = static_cast<std::uint16_t>(rows.size());
  for (const auto& row : rows) {
    a.actionBase.v.push_back(static_cast<std::uint32_t>(a.actions.v.size()));
    a.actionCount.v.push_back(static_cast<std::uint16_t>(row.size()));
    a.actions.v.insert(a.actions.v.end(), row.begin(), row.end());
  }
  a.nactions = static_cast<std::uint32_t>(a.actions.v.size());
  const auto before = a.nactions;
  lr_share_action_rows(a);
  CHECK(!a.overflow && a.nactions < before);
  CHECK(a.actionBase[1] == a.actionBase[2]);
  for (unsigned s = 0; s < rows.size(); ++s) {
    CHECK(a.actionCount[s] == rows[s].size());
    for (unsigned i = 0; i < rows[s].size(); ++i) {
      const unsigned at = a.actionBase[s] + i;
      CHECK(a.actions[at] == rows[s][i]);
      CHECK(a.action(at) == rows[s][i]);
    }
  }
  LrBuildTable empty;
  lr_share_action_rows(empty);
  CHECK(empty.nactions == 0);
}

}  // namespace action_rows

namespace lexical_derived {

void owed_terminals_stop_at_the_first_acceptance() {
  using namespace cursiv;
  LrBuildTable a;
  a.cfg.nnts = 1;
  a.cfg.nterms = 6;
  a.cfg.terms.row(0, a.cfg.nterms);
  a.cfg.terms[2].trivia = true;
  a.cfg.terms[4].trivia = true;
  lr_derive_tables(a, {});
  CHECK(a.lexOwed.empty());
  a.ndfa = 14;
  a.ncls = 3;
  a.dfaNext.v.assign(std::size_t{a.ndfa} * a.ncls, kDeadState);
  a.dfaAccept.v.assign(a.ndfa, 0);
  a.dfaAcc.row(0, a.ndfa);
  a.dfaRes.row(0, a.ndfa);
  a.dfaBound.v.assign(a.ndfa, kNoSet);
  const auto edge = [&](unsigned from, unsigned cls, std::uint16_t to) {
    a.dfaNext[from * a.ncls + cls] = to;
  };
  const auto accept = [&](unsigned s, std::uint16_t term) {
    a.dfaAccept[s] = term + 1u;
    a.dfaAcc[s].set(term);
  };
  edge(0, 0, 1);
  edge(0, 1, 2);
  edge(1, 0, 5);
  edge(2, 0, 3);
  edge(3, 0, 2);
  edge(3, 1, 5);
  edge(4, 0, 4);
  edge(6, 0, 1);
  edge(7, 0, 8);
  edge(7, 1, 9);
  edge(10, 0, 11);
  edge(10, 1, 9);
  edge(12, 0, 5);
  accept(1, 1);
  accept(5, 2);
  accept(8, 4);
  accept(9, 5);
  accept(11, 1);
  accept(12, 3);
  a.accBoundBase.v.assign(a.ndfa + 1u, 0);
  for (unsigned s = 0; s < a.ndfa; ++s)
    a.accBoundBase.v[s + 1u] =
        a.accBoundBase.v[s] + a.dfaAcc[s].rank(a.cfg.nterms);
  a.accBound.v.assign(a.accBoundBase.v[a.ndfa], kNoSet);
  a.accTier.v.assign(a.accBoundBase.v[a.ndfa], 0);
  a.accTier.v[a.accBoundBase.v[5]] = kTierKeyword;
  a.accTier.v[a.accBoundBase.v[12]] = kTierKeyword;
  lr_derive_tables(a, {});
  const std::vector<std::uint16_t> expected{
      1, 1, 2, 2, kInvalidSymbol, 2, 1, 5, 4, 5, 1, 1, 3, kInvalidSymbol};
  CHECK(a.lexOwed == expected);
}

void relay_chains_share_suffixes_in_both_pool_forms() {
  using namespace cursiv;
  std::vector<LexRelayGen> dynamic(3);
  Vec<LexRelayGen> growing;
  growing.v = dynamic;
  LexRelayChains x, y;
  const auto check = [&](std::vector<std::uint16_t> group,
                         std::uint16_t expected) {
    auto other = group;
    const auto plain = x.intern(group, dynamic, [&](const auto& row) {
      const auto at = static_cast<std::uint16_t>(dynamic.size());
      dynamic.push_back(row);
      return at;
    });
    const auto indexed = y.intern(other, growing, [&](const auto& row) {
      const auto at = static_cast<std::uint16_t>(growing.size());
      growing[at] = row;
      return at;
    });
    CHECK(plain == expected && indexed == expected);
    CHECK(dynamic == growing.v);
  };
  check({}, kNoRelay);
  check({2, 2}, 2);
  check({1, 2, 1}, 3);
  check({2, 0, 1}, 4);
  check({1, 0, 2, 0}, 4);
  CHECK(dynamic.size() == 5);
  CHECK(dynamic[3].next == 2 && dynamic[4].next == 3);
}

}  // namespace lexical_derived

// A state owes the tokens its open items still need, the fewest first: the
// bracket that closes a list is owed, the separator that would extend it is
// not.
namespace close_rows {

namespace {

constexpr std::uint16_t kOpen = 1, kClose = 2, kComma = 3, kN = 4;

// Terminal `k + 1` spells `spell[k]`; nonterminal 1 is the entry.
void spell(cursiv::LrBuildCfg& g, std::string_view spell, unsigned nnts) {
  const auto n = static_cast<std::uint16_t>(spell.size());
  for (std::uint16_t k = 0; k < n; ++k) {
    cursiv::ByteSet one{};
    one.add(static_cast<unsigned char>(spell[k]));
    g.sets[k] = one;
    g.lex[k] = cursiv::LexNode{cursiv::LexKind::chars, k, 0, 0, 0};
  }
  g.nsets = n;
  g.nlex = n;
  g.nterms = n + 1u;
  for (unsigned t = 0; t <= n; ++t) {
    g.terms[t] = cursiv::TermInfo{};
    g.terms[t].element = cursiv::kNoElement;
    g.termBuild[t] = cursiv::TermBuild{};
    g.termBuild[t].lex = static_cast<std::uint16_t>(t == 0 ? 0 : t - 1);
  }
  g.nnts = nnts;
  g.entryNt = 1;
  g.startProd = 0;
}

// V -> '[' E ']' | '[' ']' | n ;  E -> E ',' V | V
void brackets(cursiv::LrLowered& low) {
  cursiv::LrBuildCfg& g = low.cfg;
  spell(g, "[],n", 3);  // 0 = $accept, 1 = V, 2 = E
  const std::uint16_t V = cursiv::nt_sym(1), E = cursiv::nt_sym(2);
  lay(g, 0, {V});
  lay(g, 1, {kOpen, E, kClose});
  lay(g, 1, {kOpen, kClose});
  lay(g, 1, {kN});
  lay(g, 2, {E, kComma, V});
  lay(g, 2, {V});
}

// S -> L d a | L e f ;  L -> L d | d
void list_then_its_token(cursiv::LrLowered& low) {
  cursiv::LrBuildCfg& g = low.cfg;
  spell(g, "daef", 3);
  const std::uint16_t S = cursiv::nt_sym(1), L = cursiv::nt_sym(2);
  lay(g, 0, {S});
  lay(g, 1, {L, 1, 2});
  lay(g, 1, {L, 3, 4});
  lay(g, 2, {L, 1});
  lay(g, 2, {1});
}

// S -> x a c | x b c | x Y z z ;  Y -> b
void called_rule_shifts_first(cursiv::LrLowered& low) {
  cursiv::LrBuildCfg& g = low.cfg;
  spell(g, "xabcz", 3);
  const std::uint16_t S = cursiv::nt_sym(1), Y = cursiv::nt_sym(2);
  lay(g, 0, {S});
  lay(g, 1, {1, 2, 4});
  lay(g, 1, {1, 3, 4});
  lay(g, 1, {1, Y, 5, 5});
  lay(g, 2, {3});
}

std::uint32_t goto_of(const cursiv::LrBuildTable& A, std::uint32_t from,
                      std::uint16_t nt) {
  for (const auto e : A.gotoRows[from])
    if (e.nt == nt) return e.to;
  return 0xFFFFFFFFu;
}

// The state the table stands in once it has read `terms` and made every
// reduction `la` asks for, short of shifting it.
std::uint32_t before(const cursiv::LrBuildTable& A,
                     std::initializer_list<std::uint16_t> terms,
                     std::uint16_t la) {
  std::vector<std::uint32_t> stack{0};
  std::vector<std::uint16_t> input(terms);
  input.push_back(la);
  for (std::size_t i = 0; i < input.size(); ++i) {
    for (;;) {
      const std::uint32_t s = stack.back();
      const cursiv::LrAction* act = nullptr;
      for (unsigned k = 0; k < A.actionCount[s]; ++k)
        if (A.actions[A.actionBase[s] + k].term == input[i])
          act = &A.actions[A.actionBase[s] + k];
      if (act == nullptr) return 0xFFFFFFFFu;
      if (act->kind != cursiv::LrActionKind::reduce) {
        if (i + 1 == input.size()) return s;
        stack.push_back(act->arg);
        break;
      }
      const cursiv::LrLowProd& p = A.cfg.prods[act->arg];
      stack.resize(stack.size() - p.len);
      stack.push_back(goto_of(A, stack.back(), p.lhs));
    }
  }
  return stack.back();
}

bool owes(const cursiv::LrBuildTable& A, std::uint32_t s,
          std::initializer_list<std::uint16_t> terms) {
  if (s >= A.nstates) return false;
  cursiv::DynSet want;
  for (const std::uint16_t t : terms) want.set(t);
  return A.closeRow[s] == want;
}

}  // namespace

void run() {
  cursiv::LrLowered low;
  brackets(low);
  const cursiv::LrBuildTable A = cursiv::lr_build_automaton(low);
  CHECK(!A.overflow && !A.cfg.lowerFail());
  // The shortest value is a word, not a pair of brackets.
  CHECK(owes(A, before(A, {}, kOpen), {kN}));
  CHECK(owes(A, before(A, {kOpen}, kN), {kClose}));
  // The list stands after one element: the bracket closes it, and a comma
  // would open one more.
  CHECK(owes(A, before(A, {kOpen, kN}, kClose), {kClose}));
  CHECK(owes(A, before(A, {kOpen, kN, kComma}, kN), {kN}));
  // A whole value owes a reduction, and no token at all.
  CHECK(owes(A, before(A, {kOpen, kClose}, 0), {}));
  CHECK(A.closeMax == 3);
  // A due shift owes one token less for every item taking it: the list's
  // continuation and the called rule, which close in one, are what is owed.
  cursiv::LrLowered list;
  list_then_its_token(list);
  const cursiv::LrBuildTable L = cursiv::lr_build_automaton(list);
  CHECK(!L.overflow && !L.cfg.lowerFail());
  CHECK(owes(L, before(L, {1}, 3), {1}));
  cursiv::LrLowered called;
  called_rule_shifts_first(called);
  const cursiv::LrBuildTable C = cursiv::lr_build_automaton(called);
  CHECK(!C.overflow && !C.cfg.lowerFail());
  CHECK(owes(C, before(C, {1}, 3), {3}));
}

}  // namespace close_rows

namespace programs {

namespace {

void blocks_share_values_without_changing_columns() {
  using namespace cursiv;
  LrProgramMatrix m;
  const std::vector<std::vector<std::uint64_t>> rows{
      {0, 10, 10, 20, 0, 0, 0, 0, 1, 2, 3},
      {0, 30, 30, 40, 0, 0, 0, 0, 1, 2, 3},
      {0, 10, 10, 20, 0, 0, 0, 0, 1, 2, 3}};
  for (const auto& row : rows) m.append(row, 0);
  CHECK(m.rows[0] == m.rows[2] && m.rows[0] != m.rows[1]);
  CHECK(m.blocks.size() == 4 && m.blocks[1] == m.blocks[3]);
  const LrProgramRows<std::uint32_t, std::uint32_t, std::uint64_t> view{
      m.rows, m.blocks, m.values, 11, 0};
  for (unsigned state = 0; state < rows.size(); ++state) {
    for (unsigned col = 0; col < rows[state].size(); ++col)
      CHECK(view.get(state, col) == rows[state][col]);
    CHECK(view.get(state, 11) == 0);
    CHECK(view.get(state, 0xffffu) == 0);
  }
  CHECK(m.index_bits() == 8 && m.value_bits() == 8);
  // Two distinct rows of eleven values: shared rows of values are smallest.
  m.seal();
  CHECK(m.blocks.empty() && m.rows == std::vector<std::uint32_t>{0, 11, 0});
  const LrProgramRows<std::uint32_t, std::uint32_t, std::uint64_t> laid{
      m.rows, m.blocks, m.values, 11, 0};
  for (unsigned state = 0; state < rows.size(); ++state)
    for (unsigned col = 0; col < rows[state].size(); ++col)
      CHECK(laid.get(state, col) == rows[state][col]);

  LrProgramMatrix wide;
  for (unsigned i = 0; i < 65537; ++i) {
    const std::uint64_t cell = (std::uint64_t{1} << 40) + i;
    wide.append({&cell, 1}, 0);
    CHECK(wide.index_bits() == (i < 256 ? 8u : i < 65536 ? 16u : 32u));
  }
  CHECK(wide.value_bits() == 64);
  // Every row distinct, one value wide: the values alone are smallest.
  wide.seal();
  CHECK(wide.rows.empty() && wide.blocks.empty());
  const LrProgramRows<std::uint32_t, std::uint32_t, std::uint64_t> wideView{
      wide.rows, wide.blocks, wide.values, 1, 0};
  CHECK(wideView.get(65536, 0) == (std::uint64_t{1} << 40) + 65536);

  LrProgramMatrix empty;
  empty.append({}, 0xffffu);
  empty.append({}, 0xffffu);
  const LrProgramRows<std::uint32_t, std::uint32_t, std::uint64_t> emptyView{
      empty.rows, empty.blocks, empty.values, 0, 0xffffu};
  CHECK(emptyView.get(1, 0) == 0xffffu);
  CHECK(empty.index_bits() == 8 && empty.value_bits() == 8);
  CHECK(LrProgramMatrix::bits(0xffu) == 8 &&
        LrProgramMatrix::bits(0x100u) == 16 &&
        LrProgramMatrix::bits(0x10000u) == 32 &&
        LrProgramMatrix::bits(0x100000000u) == 64);
}

void programs_preserve_order_and_shift_effects() {
  using namespace cursiv;
  LrBuildTable a;
  a.cfg.nterms = 4;
  a.cfg.nnts = 3;
  a.nstates = 7;
  const LrAction shift{.term = 1, .arg = 2, .site = 3, .outer = 4};
  const LrAction reduce{.term = 1, .kind = LrActionKind::reduce, .arg = 7};
  std::vector<std::vector<LrAction>> rows{
      {reduce, shift}, {reduce, shift}, {reduce, shift}, {reduce, shift}, {},
      {shift, reduce}, {reduce, reduce}};
  rows[1][1].arg = 5;
  for (auto& x : rows[2]) x.term = 2;
  rows[3] = {reduce, reduce};
  for (auto& x : rows[6]) x.term = 2;
  for (unsigned i = 0; i < 257; ++i)
    rows[4].push_back({.term = 3,
                       .kind = LrActionKind::reduce,
                       .arg = static_cast<std::uint16_t>(i)});
  for (const auto& row : rows) {
    a.actionBase.v.push_back(static_cast<std::uint32_t>(a.actions.size()));
    a.actionCount.v.push_back(static_cast<std::uint16_t>(row.size()));
    a.actions.v.insert(a.actions.v.end(), row.begin(), row.end());
  }
  a.nactions = static_cast<std::uint32_t>(a.actions.size());
  a.gotoRows.base = {0, 2, 2, 2, 3, 3, 3, 3};
  a.gotoRows.edges = {{0, 2, 0}, {1, 1, 0}, {0, 2, 0}};
  lr_share_action_rows(a);
  const LrProgramLayout p(a);
  CHECK(p.countBits == 9);
  const LrProgramRows<std::uint32_t, std::uint32_t, std::uint64_t> actions{
      p.actions.rows, p.actions.blocks, p.actions.values, a.cfg.nterms, 0};
  for (unsigned s = 0; s < rows.size(); ++s)
    for (unsigned term = 0; term < 5; ++term) {
      std::vector<LrAction> expected;
      for (const auto& x : rows[s])
        if (x.term == term) expected.push_back(x);
      const auto cell = actions.get(s, term);
      const auto count = cell & ((1u << p.countBits) - 1);
      const auto first = cell >> p.countBits;
      CHECK(count == expected.size());
      for (unsigned i = 0; i < count; ++i) {
        const auto& want = expected[i];
        CHECK(p.ops[first + i] ==
              ((std::uint32_t{want.arg} << 2) |
               static_cast<std::uint8_t>(want.kind)));
      }
    }
  CHECK(actions.get(0, 1) != actions.get(1, 1));
  CHECK(actions.get(0, 1) != actions.get(2, 2));
  CHECK(actions.get(0, 1) != actions.get(5, 1));
  CHECK(actions.get(3, 1) == actions.get(6, 2));
  const LrProgramRows<std::uint32_t, std::uint32_t, std::uint64_t> gotos{
      p.gotos.rows, p.gotos.blocks, p.gotos.values, a.cfg.nnts, 0xffffu};
  for (unsigned s = 0; s < a.nstates; ++s)
    for (unsigned nt = 0; nt < 4; ++nt) {
      unsigned expected = 0xffffu;
      if (nt == 0 && (s == 0 || s == 3)) expected = 2;
      if (nt == 1 && s == 0) expected = 1;
      CHECK(gotos.get(s, nt) == expected);
    }
}

}  // namespace

void run() {
  blocks_share_values_without_changing_columns();
  programs_preserve_order_and_shift_effects();
}

}  // namespace programs

// A grammar built to make reduce/reduce cells the grammar does not order:
// one terminal, rules that reach back, and two bodies of different lengths
// over the same text. The table forks such a cell by production index, and
// `nOrdered` counts it -- the count is what this pins. A body past 255
// symbols is ordered at its far dots as at its near ones.
namespace ordered_reduce {

namespace {

// S -> a | B a a ;  B -> a | S a | C S a ;  C -> a | a a a
void cyclic(cursiv::LrLowered& low) {
  cursiv::LrBuildCfg& g = low.cfg;
  cursiv::ByteSet one{};
  one.add('a');
  g.sets[0] = one;
  g.nsets = 1;
  g.nlex = 1;
  g.lex[0] = cursiv::LexNode{cursiv::LexKind::chars, 0, 0, 0, 0};
  g.nterms = 2;
  for (unsigned t = 0; t < 2; ++t) {
    g.terms[t] = cursiv::TermInfo{};
    g.terms[t].element = cursiv::kNoElement;
    g.termBuild[t] = cursiv::TermBuild{};
    g.termBuild[t].lex = 0;
  }
  g.nnts = 4;  // 0 = $accept, 1 = S, 2 = B, 3 = C
  g.entryNt = 1;
  g.startProd = 0;
  const std::uint16_t S = cursiv::nt_sym(1), B = cursiv::nt_sym(2),
                      C = cursiv::nt_sym(3), a = 1;
  lay(g, 0, {S});
  lay(g, 1, {a});
  lay(g, 1, {B, a, a});
  lay(g, 2, {a});
  lay(g, 2, {S, a});
  lay(g, 2, {C, S, a});
  lay(g, 3, {a});
  lay(g, 3, {a, a, a});
}

void unordered_cells_are_counted() {
  cursiv::LrLowered low;
  cyclic(low);
  const cursiv::LrBuildTable A = cursiv::lr_build_automaton(low);
  CHECK(!A.overflow && !A.cfg.lowerFail());
  CHECK(A.nOrdered != 0);
}

// A body longer than 255 symbols that meets a reduction at its dots 5 and
// 261, under two callers whose cells stand over different origins. `P` is
// written first, so its shift precedes.
struct LongBodies : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  static constexpr auto k5 =
      lit<"k"> + lit<"k"> + lit<"k"> + lit<"k"> + lit<"k">;
  static constexpr auto k50 = k5 + k5 + k5 + k5 + k5 + k5 + k5 + k5 + k5 + k5;
  static constexpr auto k255 = k50 + k50 + k50 + k50 + k50 + k5;
  struct P : rule<k5 + lit<"x"> + k255 + lit<"x"> + ref<NUM>> {};
  struct C : rule<k5> {};
  struct D : rule<k5 + lit<"x"> + k255> {};
  struct X : rule<ref<P> | (ref<C> + lit<"x"> + ref<NUM>) |
                  (ref<D> + lit<"x"> + ref<NUM>)> {};
  struct W : rule<ref<P> | (ref<C> + lit<"x"> + ref<NUM>)> {};
  struct Root : rule<(lit<"a"> + ref<X> + lit<"y">) |
                     (lit<"b"> + ref<W> + lit<"z">)> {};
  using entry = Root;
  using ignored = WS;
};

// Past dot 255 a body is ordered as before it: where `D` ends, the shift
// of `P` precedes its reduction for every text.
void long_bodies_keep_the_order_written() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<LongBodies>::table());
  CHECK(!A.overflow && !A.cfg.lowerFail());
  unsigned cells = 0;
  for (std::uint32_t s = 0; s < A.nstates; ++s)
    for (unsigned k = 1; k < A.actionCount[s]; ++k) {
      const cursiv::LrAction& first = A.actions[A.actionBase[s] + k - 1];
      const cursiv::LrAction& reduce = A.actions[A.actionBase[s] + k];
      if (first.term != reduce.term ||
          reduce.kind != cursiv::LrActionKind::reduce ||
          A.cfg.prods[reduce.arg].len < 256)
        continue;
      ++cells;
      CHECK(cursiv::lr_takes_token(first.kind));
      CHECK(first.exact && reduce.exact);
    }
  CHECK(cells == 1);
}

}  // namespace

void run() {
  unordered_cells_are_counted();
  long_bodies_keep_the_order_written();
}

}  // namespace ordered_reduce

// The split of a record's stamp: what the generator chooses from a table's
// counts, where it refuses, and that every split reads back what it wrote.
namespace stamp_layout {

namespace {

using cursiv::stamp_split;
using cursiv::StampLayout;

constexpr std::optional<StampLayout> kNone;

void a_table_whose_states_leave_room_keeps_the_full_row() {
  static_assert(StampLayout::record_bits() == 30);
  static_assert(StampLayout{}.rowBits == 16);
  CHECK(stamp_split(150, 60) == StampLayout{});
  CHECK(stamp_split(65535, 1) == StampLayout{});
  CHECK(stamp_split(0, 16384) == StampLayout{});
}

void past_fourteen_bits_of_states_the_row_yields() {
  CHECK(stamp_split(150, 16385) == StampLayout{15});
  CHECK(stamp_split(32767, 32768) == StampLayout{15});
  CHECK(stamp_split(16383, 65535) == StampLayout{14});
}

// The sentinel is the row's full mask, so a row width holds one row fewer
// than it counts.
void rows_and_states_that_do_not_fit_together_are_refused() {
  CHECK(stamp_split(65536, 1) == kNone);
  CHECK(stamp_split(32768, 16385) == kNone);
  CHECK(stamp_split(16384, 65535) == kNone);
}

void a_forced_row_is_honoured_or_refused() {
  CHECK(stamp_split(150, 60, 8) == StampLayout{8});
  CHECK(stamp_split(255, 60, 8) == StampLayout{8});
  CHECK(stamp_split(256, 60, 8) == kNone);
  CHECK(stamp_split(150, 60, 7) == kNone);
  CHECK(stamp_split(150, 65535, 16) == kNone);
  CHECK(stamp_split(150, 60, 17) == kNone);
}

void the_refusal_names_both_counts() {
  CHECK(cursiv::stamp_refusal(40000, 20000) ==
        "its 40000 AST write chains need 16 bits and its 20000 parser states "
        "15 bits, more than the 30 bits of a record");
  CHECK(cursiv::stamp_refusal(150, 60, 7) ==
        "its 150 AST write chains need 8 bits and its 60 parser states 6 "
        "bits, more than the 30 bits of a record with the chains forced to 7 "
        "bits");
}

// Row and state read back whatever the split, the sentinel included, and a
// rewrite of one leaves the other.
void every_split_reads_back_what_it_wrote() {
  for (const unsigned bits : {1u, 8u, 9u, 14u, 15u, 16u}) {
    const StampLayout L{bits};
    const auto top = static_cast<cursiv::ElementId>(L.row_mask() - 1u);
    const unsigned stateBits = std::min(L.state_bits(), 16u);
    const auto wide = static_cast<std::uint16_t>((1u << stateBits) - 1u);
    cursiv::CstNode n{};
    n.engine = L.make(top, wide);
    CHECK(L.row(n) == top);
    CHECK(L.state(n) == wide);
    n.engine = L.make(cursiv::kNoElement, 0);
    CHECK(L.row(n) == cursiv::kNoElement);
    CHECK(L.state(n) == 0);
    L.set_state(n, wide);
    CHECK(L.row(n) == cursiv::kNoElement);
    CHECK(L.state(n) == wide);
    L.set_row(n, 0);
    CHECK(L.row(n) == 0);
    CHECK(L.state(n) == wide);
    L.set_row(n, cursiv::kNoElement);
    CHECK(L.row(n) == cursiv::kNoElement);
    CHECK(L.state(n) == wide);
  }
}

}  // namespace

void run() {
  a_table_whose_states_leave_room_keeps_the_full_row();
  past_fourteen_bits_of_states_the_row_yields();
  rows_and_states_that_do_not_fit_together_are_refused();
  a_forced_row_is_honoured_or_refused();
  the_refusal_names_both_counts();
  every_split_reads_back_what_it_wrote();
}

}  // namespace stamp_layout

namespace lower_limits {

namespace {
using namespace cursiv;

// Whether `build` is refused, and for `why`.
template <class Build>
bool refused(Build build, LowerFail why) {
  try {
    build();
  } catch (const LowerRefused& e) {
    return e.why == why;
  }
  return false;
}

void name_pool_refuses_before_returning_an_invalid_span() {
  LrBuildCfg c;
  CHECK(lr_intern_name(c, {}) == 0);
  CHECK(c.nameLen == 0 && c.names.size() == 0);
  const std::string text(65535, 'x');
  CHECK(lr_intern_name(c, text) == 0);
  CHECK(c.nameLen == 65535 && c.names.size() == 65535);
  CHECK(c.names[0] == 'x' && c.names[65534] == 'x');
  c.terms[0].nameLen = 65535;
  CHECK(c.term_name(0) == text);
  c.terms[1].nameOff = 65535;
  CHECK(lr_intern_name(c, {}) == 65535 && c.term_name(1).empty());
  c.terms[2].nameOff = 65534;
  c.terms[2].nameLen = 2;
  CHECK(c.term_name(2).empty());
  CHECK(refused([&] { (void)lr_intern_name(c, "y"); }, LowerFail::namePool) &&
        c.lowerWhy == LowerFail::namePool);
  CHECK(c.nameLen == 65535 && c.names.size() == 65535);
  CHECK(c.term_name(0) == text);
  LrBuildCfg empty;
  CHECK(refused([&] { (void)lr_intern_name(empty, std::string(65536, 'z')); },
                LowerFail::namePool) &&
        empty.nameLen == 0 && empty.names.size() == 0);
}

struct GapGrammar : GrammarSpec {
  struct Root : rule<gap::none + lit<"x">> {};
  using entry = Root;
};

// The final table is the one that was built, derived and laid out, never a
// copy; the emitters read it alone, and nothing writes to it or runs a pass
// over it.
template <class T>
concept WritesStates = requires(T& t) { t.nstates = 0; };
template <class T>
concept MinimizesStates = requires(T& t) { lr_minimize_states(t); };
using FinalTable = std::remove_reference_t<
    decltype(std::declval<const LrFinalTable&>().table())>;
static_assert(WritesStates<LrBuildTable> && MinimizesStates<LrBuildTable>);
static_assert(!WritesStates<FinalTable> && !MinimizesStates<FinalTable>);
static_assert(!std::is_copy_constructible_v<LrFinalTable>);

void a_final_table_is_the_built_one_read_only() {
  auto graph = std::make_unique<LrBuildTable>(
      lr_build_automaton(LrLowering<GapGrammar>::table()));
  const LrBuildTable* const owner = graph.get();
  const LrAction* const actions = graph->actions.data();
  const auto* const prods = graph->cfg.prods.data();
  const std::vector<ElementId> sites(graph->writes.nchains + graph->nfused,
                                     kNoElement);
  const LrFinalTable done = LrFinalTable::make(std::move(graph), sites);
  CHECK(&done.table() == owner);
  CHECK(done.table().actions.data() == actions);
  CHECK(done.table().cfg.prods.data() == prods);
  CHECK(done.table().cfg.ngapConds == 1 && done.table().cfg.ngapLeaves == 1);
  CHECK(!lr_emit_table(done, "final").empty());
  CHECK(!lr_emit_parser_decl(done, "final").empty());
  CHECK(!lr_emit_parser_files(done, "final").empty());
}

struct OversizedLexicalDfa : GrammarSpec {
  struct T : terminal<repeat<65535, 65535>(cr<"a">)> {};
  struct Root : rule<ref<T>> {};
  using entry = Root;
};

void lexical_state_indices_are_checked_before_narrowing() {
  using Oversized = LrLowering<OversizedLexicalDfa>;
  CHECK(refused([] { (void)lr_build_automaton(Oversized::table()); },
                LowerFail::tokenGraphTooLarge));
}

template <std::size_t Min, std::size_t Max>
struct Repeated : GrammarSpec {
  struct Root : rule<repeat<Min, Max>(lit<"a">)> {};
  using entry = Root;
};

void bounds_are_checked_before_narrowing() {
  CHECK((LrLowering<Repeated<0, 65536>>::cfg().lowerWhy ==
         LowerFail::productionTooWide));
  // These bounds are not representable on a 32-bit target. A non-template
  // if constexpr still requires its discarded branch to be well-formed.
#if SIZE_MAX > UINT32_MAX
  constexpr std::size_t large = std::size_t{1} << 32;
  CHECK((LrLowering<Repeated<large + 1, kUnbounded>>::cfg().lowerWhy ==
         LowerFail::productionTooWide));
  CHECK((LrLowering<Repeated<0, large + 1>>::cfg().lowerWhy ==
         LowerFail::productionTooWide));
  CHECK(
      (LrLowering<Repeated<0, std::numeric_limits<std::uint32_t>::max()>>::cfg()
           .lowerWhy == LowerFail::productionTooWide));
#endif
  // UINT32_MAX itself denotes kUnbounded when size_t is 32 bits.
  CHECK((LrLowering<
             Repeated<0, std::numeric_limits<std::uint32_t>::max() - 1>>::cfg()
             .lowerWhy == LowerFail::productionTooWide));
}

void a_bounded_repetition_costs_two_productions_a_turn() {
  const auto wide = LrLowering<Repeated<1, 6000>>::cfg();
  CHECK(wide.lowerWhy == LowerFail::none);
  CHECK(wide.nprods <= 2 * 6000 + 8);
  CHECK((LrLowering<Repeated<3, 20000>>::cfg().lowerWhy == LowerFail::none));
  CHECK((LrLowering<Repeated<0, 40000>>::cfg().lowerWhy ==
         LowerFail::tooManyRules));
}

void edge_options_stay_linear() {
  for (const unsigned kind : {0u, 1u, 2u}) {
    LrLowered low;
    low.cfg.nterms = 2;
    low.cfg.nnts = 2;
    LrLowerPool pool;
    pool.owner = 1;
    std::vector<LrLowerSlot> slots(32);
    for (auto& s : slots) {
      s.sym = 1;
      s.optional = true;
      s.lead = kind == 0;
      s.trail = kind == 1;
      s.endAlt = kind == 2;
    }
    lr_emit_subsets(low, pool, 1, slots, {});
    CHECK(!low.cfg.lowerFail());
    CHECK(low.cfg.nprods <= 2 * slots.size() + (1u << kOptionalGate));
  }
}

void long_optional_bodies_stay_linear() {
  for (const unsigned count : {30u, 128u, 512u}) {
    LrLowered low;
    low.cfg.nterms = count + 1;
    low.cfg.nnts = 2;
    LrLowerPool pool;
    pool.owner = 1;
    std::vector<LrLowerSlot> slots(count);
    for (unsigned i = 0; i < count; ++i) {
      slots[i].sym = i + 1;
      slots[i].optional = true;
    }
    lr_emit_subsets(low, pool, 1, slots, {}, 42);
    CHECK(!low.cfg.lowerFail());
    CHECK(low.cfg.nprods <= 2 * count + (1u << kOptionalGate));
    CHECK(low.cfg.nnts <= count + 2);
    CHECK(lr_nullable(low.cfg, false)[1]);
    std::vector<bool> present(count + 1);
    for (unsigned p = 0; p < low.cfg.nprods; ++p) {
      const auto& prod = low.cfg.prods[p];
      for (unsigned i = 0; i < prod.len; ++i) {
        const auto sym = low.cfg.sym_at(prod, i);
        if (is_term(sym) && sym <= count) present[sym] = true;
      }
      if (low.cfg.prods[p].lhs == 1) {
        CHECK(low.cfg.prods[p].len <= count);
        CHECK(low.cfg.prodBuild[p].priorityAlt == 42);
      }
    }
    for (unsigned t = 1; t <= count; ++t) CHECK(present[t]);
  }
}

void factored_options_write_only_on_the_present_arm() {
  LrLowered low;
  low.cfg.nterms = 2;
  low.cfg.nnts = 2;
  LrLowerPool pool;
  pool.owner = 1;
  std::vector<LrLowerSlot> slots(kOptionalGate + 1);
  for (auto& s : slots) {
    s.sym = 1;
    s.optional = true;
    s.cfg = 1;
  }
  // Distinct action occurrences remain distinct, including a repeated row.
  slots.back().made = {17};
  slots.back().row = {23, 23};
  // Surplus positions, taken from the right, use a nullable auxiliary.
  lr_emit_subsets(low, pool, 1, slots, {});
  lr_lay_bare_records(low);
  CHECK(!low.cfg.lowerFail());
  CHECK(low.cfg.nnts == 3);
  bool present = false, absent = false, called = false;
  for (std::uint16_t p = 0; p < low.cfg.nprods; ++p) {
    const auto& q = low.cfg.prods[p];
    if (q.lhs == 2) {
      if (q.len == 0) {
        absent = true;
        CHECK(q.node == kNoRow);
      } else {
        present = true;
        CHECK(q.len == 1 && low.cfg.sym_at(q, 0) == 1);
        CHECK(low.cfg.cfg_at(p, 0) != 0);
        const auto row = low.cfg.row_at(q, 0);
        CHECK(row != kNoRow);
        if (row == kNoRow) continue;
        const auto chain = low.writes.chains[row];
        CHECK(chain.count == 3);
        if (chain.count != 3) continue;
        CHECK(low.writes.acts[chain.off] == 17);
        CHECK(low.writes.acts[chain.off + 1] == 23);
        CHECK(low.writes.acts[chain.off + 2] == 23);
      }
    } else {
      for (unsigned i = 0; i < q.len; ++i) {
        if (low.cfg.sym_at(q, i) != nt_sym(2)) continue;
        called = true;
        CHECK(low.cfg.row_at(q, i) == kNoRow);
        CHECK(low.cfg.cfg_at(p, i) != 0);
      }
    }
  }
  CHECK(present && absent && called);
}

struct EmptyGrammar : GrammarSpec {
  struct Root : rule<eof> {};
  using entry = Root;
};

void an_empty_alphabet_builds() {
  const auto a = lr_build_automaton(LrLowering<EmptyGrammar>::table());
  CHECK(!a.overflow && !a.cfg.lowerFail());
  CHECK(a.cfg.nterms == 1);
  CHECK(a.ndfa != 0);
}

template <auto Body>
struct WordPastMatch : GrammarSpec {
  struct HASH : terminal<Body> {};
  struct Root : rule<ref<HASH>> {};
  using entry = Root;
};

void a_word_read_past_a_match_is_refused() {
  const auto refused = []<auto Body>() {
    const auto a =
        lr_build_automaton(LrLowering<WordPastMatch<Body>>::table());
    CHECK(a.cfg.lowerFail());
    CHECK(a.cfg.lowerWhy == LowerFail::wordPastMatch);
  };
  refused.template operator()<lit<"##", !lit<"abc">> |
                              (cr<"#"> + lit<"#", !lit<"abc">>)>();
  refused.template operator()<lit<"#"> | lit<"##", !lit<"abc">> | lit<"##">>();
  refused.template operator()<lit<"##", !lit<"abc">> | lit<"##", !lit<"def">>>();
  refused.template operator()<some(cr<"a">) + !lit<"bc">>();
}

void a_word_behind_a_fixed_head_is_asked_by_a_matcher() {
  const auto a = lr_build_automaton(
      LrLowering<WordPastMatch<cr<"#"> + lit<"#", !lit<"abc">>>>::table());
  CHECK(!a.overflow && !a.cfg.lowerFail());
  bool asked = false;
  for (const HeadMatcher& h : a.termHeads)
    asked = asked || (h.offset == 2 && h.words.size() == 1);
  CHECK(asked);
}

void a_word_refused_at_the_head_of_every_branch_is_asked_once() {
  const auto a = lr_build_automaton(LrLowering<WordPastMatch<
      !lit<"\xef\xbb\xbf"> + (any | lit<"x">)>>::table());
  CHECK(!a.overflow && !a.cfg.lowerFail());
  CHECK(std::ranges::any_of(a.termHeads, [](const HeadMatcher& h) {
    return h.offset == 0 && h.words.size() == 1;
  }));
}

void a_word_the_graph_reads_whole_stays_in_it() {
  const auto a = lr_build_automaton(LrLowering<WordPastMatch<
      lit<"<!"> + !lit<"--"> + some(cr<"a-z">) + lit<">">>>::table());
  CHECK(!a.overflow && !a.cfg.lowerFail());
  for (const HeadMatcher& h : a.termHeads) CHECK(h.words.empty());
}

void malformed_unicode_classes_are_refused() {
  for (const std::string_view spec : {R"(é-\)", R"(é\)", R"(é\q)", R"(é\d-z)",
                                      "\xC3(", "\xE0\x80\xAF", "\xED\xA0\x80",
                                      "\xF4\x90\x80\x80", "\xF5\x80\x80\x80",
                                      "\xC3"}) {
    bool refused = false;
    try {
      (void)LexCodePoints::of(0, false, spec);
    } catch (const std::invalid_argument&) { refused = true; }
    CHECK(refused);
  }
  const auto valid = LexCodePoints::of(0, false, "é-ê");
  CHECK(valid.ranges.size() == 1);
  CHECK(valid.ranges[0].lo == U'é' && valid.ranges[0].hi == U'ê');
  const auto slash = LexCodePoints::of(0, false, R"(é\\)");
  CHECK(slash.ranges.size() == 2 && slash.ranges[0].lo == U'\\');
}

void auxiliary_self_markers_are_outside_the_symbol_domain() {
  LrBuildCfg cfg;
  cfg.nnts = kNtBase;
  cfg.auxCount = 3;
  cfg.nprods = 3;
  cfg.nslots = 3;
  for (std::uint16_t p = 0; p < 3; ++p) {
    cfg.prods[p] = {
        .lhs = static_cast<std::uint16_t>(kNtBase - 3 + p), .at = p, .len = 1};
    cfg.prodBuild[p] = {};
    cfg.slots[p] = {};
    cfg.slotCfg[p] = 0;
  }
  // The last legal symbol (0xffff) is a call in A, a self reference in B.
  cfg.slotSym[0] = nt_sym(kNtBase - 1);
  cfg.slotSym[1] = nt_sym(kNtBase - 2);
  cfg.slotSym[2] = 1;
  lr_intern_aux(cfg);
  CHECK(cfg.nprods == 3);
  CHECK(cfg.slotSym[0] == nt_sym(kNtBase - 1));
  CHECK(cfg.slotSym[1] == nt_sym(kNtBase - 2));
}

void first_sets_include_the_last_nonterminal() {
  LrBuildCfg cfg;
  cfg.nnts = kNtBase;
  cfg.nterms = 2;
  cfg.nprods = 2;
  cfg.nslots = 2;
  cfg.prodBuild.row(0, cfg.nprods);
  const auto last = static_cast<std::uint16_t>(kNtBase - 1);
  cfg.prods[0] = {.lhs = 0, .at = 0, .len = 1};
  cfg.prods[1] = {.lhs = last, .at = 1, .len = 1};
  cfg.slotSym[0] = nt_sym(last);
  cfg.slotSym[1] = 1;
  const auto fs = lr_first_sets(cfg);
  CHECK(fs.first[0].test(1));
  CHECK(fs.first[last].test(1));
}

void unit_cycle_checks_use_bounded_stack_space() {
  LrLowered low;
  auto& cfg = low.cfg;
  cfg.nnts = cfg.nprods = kNtBase;
  cfg.nslots = kNtBase;
  for (std::uint16_t p = 0; p < kNtBase; ++p) {
    cfg.prods[p] = {.lhs = p, .at = p, .len = 1};
    cfg.slotSym[p] = p + 1 == kNtBase ? 1 : nt_sym(p + 1);
  }
  const std::vector<std::uint8_t> empty(kNtBase, 0);
  lr_no_rule_without_production(low);
  CHECK(!low.fault);
  lr_no_unit_cycle(low, empty);
  CHECK(!low.fault);
  cfg.slotSym[kNtBase - 1] = nt_sym(1);
  lr_no_unit_cycle(low, empty);
  CHECK(low.fault.code == GrammarFaultCode::unitProductionCycle);
  CHECK(low.fault.rule != 0);  // The augmented start is outside the cycle.
}

void left_recursion_splitting_uses_bounded_stack_space() {
  LrLowered low;
  auto& cfg = low.cfg;
  constexpr unsigned length = 16000;
  cfg.nnts = length + 1;
  cfg.nprods = length + 2;
  cfg.prods[0] = {.lhs = 0, .at = 0, .len = 1};
  cfg.slotSym[0] = nt_sym(1);
  cfg.leftLoop[0] = kNoRow;
  // Every turn consumes x; A1 also has a finite base. The cycle is legal
  // left recursion, not a non-consuming unit cycle.
  for (unsigned i = 1; i <= length; ++i) {
    cfg.leftLoop[i] = 0;
    cfg.prods[i] = {.lhs = static_cast<std::uint16_t>(i),
                     .at = 2u * i - 1u, .len = 2};
    cfg.slotSym[2u * i - 1u] = nt_sym(i == length ? 1u : i + 1u);
    cfg.slotSym[2u * i] = 1;
  }
  cfg.prods[length + 1u] = {.lhs = 1, .at = 2u * length + 1u, .len = 1};
  cfg.slotSym[2u * length + 1u] = 1;
  lr_split_loops(low);
  CHECK(cfg.nnts == length + 1u && cfg.nprods == length + 2u);
  for (unsigned i = 1; i <= length; ++i)
    CHECK(cfg.sym_at(cfg.prods[i], 0) == nt_sym(i == length ? 1u : i + 1u));
}

void left_recursion_splitting_restores_each_siblings_path() {
  LrLowered low;
  auto& cfg = low.cfg;
  cfg.nnts = 4;
  cfg.nprods = 7;
  cfg.nts.row(0, cfg.nnts);
  cfg.prodBuild.row(0, cfg.nprods);
  const std::vector<std::vector<std::uint16_t>> bodies{
      {nt_sym(1), nt_sym(2)}, {nt_sym(2), 1}, {nt_sym(3), 2},
      {3}, {nt_sym(3), 4}, {nt_sym(1), 5}, {nt_sym(1), 6}};
  const unsigned lhs[] = {0, 1, 1, 1, 2, 2, 3};
  for (unsigned p = 0; p < bodies.size(); ++p) {
    cfg.prods[p] = {.lhs = static_cast<std::uint16_t>(lhs[p]),
                     .at = cfg.nslots,
                     .len = static_cast<std::uint16_t>(bodies[p].size())};
    for (const auto sym : bodies[p]) {
      cfg.slotSym[cfg.nslots] = sym;
      cfg.slots[cfg.nslots] = {};
      cfg.slotCfg[cfg.nslots++] = 0;
    }
  }
  cfg.leftLoop.v = {kNoRow, 0, 0, 0};
  lr_split_loops(low);
  CHECK(cfg.nnts == 10 && cfg.nprods == 18 && cfg.auxCount == 6);
  CHECK(cfg.sym_at(cfg.prods[1], 0) == nt_sym(4));
  CHECK(cfg.sym_at(cfg.prods[2], 0) == nt_sym(5));
  CHECK(cfg.sym_at(cfg.prods[4], 0) == nt_sym(6));
  CHECK(cfg.sym_at(cfg.prods[5], 0) == nt_sym(8));
  CHECK(cfg.sym_at(cfg.prods[6], 0) == nt_sym(1));
}

LrBuildTable election_aliases(unsigned length) {
  LrBuildTable a;
  auto& cfg = a.cfg;
  cfg.nterms = 3;
  cfg.nnts = cfg.nprods = static_cast<std::uint16_t>(length + 2u);
  const auto lay = [&](unsigned p, const std::vector<std::uint16_t>& body) {
    cfg.prods[p] = {.lhs = static_cast<std::uint16_t>(p),
                     .at = cfg.nslots,
                     .len = static_cast<std::uint16_t>(body.size())};
    cfg.prodBuild[p] = {};
    for (const auto sym : body) {
      cfg.slotSym[cfg.nslots] = sym;
      cfg.slots[cfg.nslots] = {};
      cfg.slotCfg[cfg.nslots++] = 0;
    }
  };
  lay(0, {nt_sym(1), nt_sym(static_cast<std::uint16_t>(length + 1u))});
  for (unsigned p = 1; p <= length; ++p)
    lay(p, {p == length ? std::uint16_t{1}
                       : nt_sym(static_cast<std::uint16_t>(p + 1u))});
  lay(length + 1u, {1, 2});
  return a;
}

void wordless_aliases_need_no_election_paths() {
  auto a = election_aliases(2048);
  // A separate, non-unit production spells a decision, so the table's
  // election pass runs even though the long alias component has none.
  a.electAt.assign(a.cfg.nprods + 1u, 0);
  a.electAt.back() = 1;
  a.electWord = {900};
  a.electPos = {0};
  lr_elect_runs(a);
  CHECK(a.electWord == std::vector<std::uint32_t>{900});
  CHECK(a.electChainKey.empty() && a.electChainWord.empty());
  CHECK(a.electWatch.empty() && a.electTwin.empty());
}

void alias_decisions_keep_their_order_around_the_callee() {
  auto a = election_aliases(8);
  a.electAt.push_back(0);
  for (unsigned p = 0; p < a.cfg.nprods; ++p) {
    if (p == 1 || p == 3) {
      a.electWord.insert(a.electWord.end(), {10u * p + 1u, 10u * p + 2u});
      a.electPos.insert(a.electPos.end(), {0, 1});
    }
    a.electAt.push_back(static_cast<std::uint32_t>(a.electWord.size()));
  }
  lr_elect_runs(a);
  const auto chain = [&](unsigned from, unsigned to) {
    const auto key = from << 16 | to;
    const auto at = std::lower_bound(a.electChainKey.begin(),
                                      a.electChainKey.end(), key);
    CHECK(at != a.electChainKey.end() && *at == key);
    if (at == a.electChainKey.end() || *at != key)
      return std::vector<std::uint32_t>{};
    const auto i = static_cast<unsigned>(at - a.electChainKey.begin());
    const auto begin = a.electChainAt[i], end = a.electChainAt[i + 1u];
    bool after = false;
    for (auto k = begin; k < end; ++k) {
      CHECK(a.electChainPos[k] <= 1);
      CHECK(!after || a.electChainPos[k] == 1);
      after = a.electChainPos[k] == 1;
    }
    return std::vector<std::uint32_t>(a.electChainWord.begin() + begin,
                                      a.electChainWord.begin() + end);
  };
  CHECK(chain(1, a.cfg.nterms + 4u) ==
        std::vector<std::uint32_t>({11, 31, 32, 12}));
  CHECK(chain(2, a.cfg.nterms + 4u) == std::vector<std::uint32_t>({31, 32}));
  CHECK(chain(4, 1).empty());
  CHECK(!a.electWatch.empty());
}

void lexical_pools_cannot_wrap() {
  for (unsigned which = 0; which < 3; ++which) {
    LrBuildCfg cfg;
    detail::SetIndex ix;
    CHECK(refused(
        [&] {
          if (which == 0) {
            cfg.nlex = 65535;
            cfg.push_lex({});
          } else if (which == 1) {
            cfg.nlexKid = 65535;
            cfg.push_lex_kid(0);
          } else {
            cfg.nsets = 65535;
            detail::intern_set(cfg, ix, {});
          }
        },
        LowerFail::tokenGraphTooLarge));
  }
  // Guard products also intern sets after the initial lexical lowering.
  using Guard =
      WordPastMatch<some(cr<"#">) + !lit<"abc"> + !cr<"b"> + cr<"a-d">>;
  auto low = LrLowering<Guard>::table();
  low.cfg.sets[65534] = {};
  low.cfg.setDecode[65534] = 0;
  low.cfg.nsets = 65535;
  CHECK(refused([&] { (void)lr_build_automaton(low); },
                LowerFail::tokenGraphTooLarge));

}
}  // namespace

void run() {
  name_pool_refuses_before_returning_an_invalid_span();
  a_final_table_is_the_built_one_read_only();
  lexical_state_indices_are_checked_before_narrowing();
  bounds_are_checked_before_narrowing();
  a_bounded_repetition_costs_two_productions_a_turn();
  edge_options_stay_linear();
  factored_options_write_only_on_the_present_arm();
  long_optional_bodies_stay_linear();
  an_empty_alphabet_builds();
  a_word_read_past_a_match_is_refused();
  a_word_behind_a_fixed_head_is_asked_by_a_matcher();
  a_word_refused_at_the_head_of_every_branch_is_asked_once();
  a_word_the_graph_reads_whole_stays_in_it();
  auxiliary_self_markers_are_outside_the_symbol_domain();
  first_sets_include_the_last_nonterminal();
  unit_cycle_checks_use_bounded_stack_space();
  left_recursion_splitting_uses_bounded_stack_space();
  left_recursion_splitting_restores_each_siblings_path();
  wordless_aliases_need_no_election_paths();
  alias_decisions_keep_their_order_around_the_callee();
  lexical_pools_cannot_wrap();
  malformed_unicode_classes_are_refused();
}

}  // namespace lower_limits

namespace fold_contract {
using namespace cursiv;

void precomposition_keeps_bare_records_through_renames() {
  for (bool wrappedLeaf : {false, true}) {
    LrBuildTable a;
    auto& g = a.cfg;
    g.nnts = 3;
    g.nprods = 4;
    g.nterms = 3;
    a.writes.nchains = 4;
    g.prods.row(0, 4);
    g.prodBuild.row(0, 4);
    g.slots.row(0, 4);
    g.slotSym.row(0, 4);
    g.slotCfg.row(0, 4);
    g.terms.row(0, 3);
    g.terms[1].element = kNoRow;
    g.terms[2].element = wrappedLeaf ? 2 : 1;
    for (unsigned p = 0; p < 4; ++p) g.prods[p] = {.lhs = 0, .at = p, .len = 1};
    g.slotSym[0] = 1;
    g.slotSym[1] = 2;
    if (wrappedLeaf) {
      g.prods[0].node = 3;
      g.slots[0].row = 2;
      g.prods[1].lhs = 1;
      g.prods[1].node = 3;
    } else {
      g.prods[2].lhs = 1;
      g.slots[2].row = 2;
    }
    g.slotSym[2] = nt_sym(0);
    g.prods[3].lhs = 2;
    g.slotSym[3] = nt_sym(wrappedLeaf ? 0 : 1);
    g.slots[3].row = 1;
    CHECK(lr_fold_chains(a, LrLowered{}));
    LrDerivedRows plans;
    lr_derive_reductions(a, plans);
    const auto folds = [&](ElementId held, ElementId over) {
      for (unsigned i = 0; i < a.nfused; ++i)
        if (a.fused[i].node == held && a.fused[i].site == over)
          return static_cast<ElementId>(a.writes.nchains + i);
      return kNoRow;
    };
    for (ElementId leaf : {kNoRow, ElementId{1}}) {
      const auto write = [&](bool planned) {
        Cst tree("x");
        CstBuilder builder(tree);
        LrContext c("x", builder);
        ElementId held = wrappedLeaf ? folds(2, 3) : leaf;
        CHECK(!wrappedLeaf || held != kNoRow);
        c.tape.leaf<StampLayout{}>(0, 1, held, 0);
        LrFrame frame{0, held, 0, 0, 0};
        for (unsigned prod = wrappedLeaf ? 3 : 2; prod < 4; ++prod) {
          const auto& n = plans.reduceNames[plans.reductions[prod].names];
          if (planned && !n.fold)
            lr_name_row_at(c, frame, n.row);
          else
            lr_name_row(c, frame, g.slots[prod].row, folds);
        }
        return c.tape.at(0).engine;
      };
      CHECK(write(true) == write(false));
    }
  }
}

void substituted_reductions_keep_the_returned_row() {
  LrBuildTable a;
  auto& g = a.cfg;
  g.nnts = 3;
  g.nprods = 4;
  g.nterms = 3;
  a.writes.nchains = 4;
  g.prods.row(0, 4);
  g.prodBuild.row(0, 4);
  g.slots.row(0, 4);
  g.slotSym.row(0, 4);
  g.slotCfg.row(0, 4);
  g.terms.row(0, 3);
  g.terms[1].element = 1;
  g.terms[2].element = 2;
  g.prods[0] = {.lhs = 0, .at = 0, .len = 1};
  g.prods[1] = {.lhs = 1, .at = 1, .len = 1};
  g.prods[2] = {.lhs = 2, .at = 2, .len = 1};
  g.prods[3] = {.lhs = 2, .at = 3, .len = 1};
  g.slotSym[0] = 1;
  g.slotSym[1] = 2;
  g.slotSym[2] = nt_sym(0);
  g.slotSym[3] = nt_sym(1);
  g.slots[2].row = g.slots[3].row = 3;
  std::vector<std::uint32_t> objectKey, arrayKey;
  lr_reduce_key(g, 2, objectKey);
  lr_reduce_key(g, 3, arrayKey);
  CHECK(objectKey == arrayKey);
  CHECK(lr_fold_chains(a, LrLowered{}));
  LrDerivedRows plans;
  lr_derive_reductions(a, plans);
  const auto folds = [&](ElementId held, ElementId over) {
    for (unsigned i = 0; i < a.nfused; ++i)
      if (a.fused[i].node == held && a.fused[i].site == over)
        return static_cast<ElementId>(a.writes.nchains + i);
    return kNoRow;
  };
  for (ElementId held : {ElementId{1}, ElementId{2}}) {
    const auto write = [&](bool planned) {
      Cst tree("x");
      CstBuilder builder(tree);
      LrContext c("x", builder);
      c.tape.leaf<StampLayout{}>(0, 1, held, 0);
      LrFrame frame{0, held, 0, 0, 0};
      const auto& n = plans.reduceNames[plans.reductions[2].names];
      if (planned && !n.fold)
        lr_name_row_at(c, frame, n.row);
      else
        lr_name_row(c, frame, 3, folds);
      return c.tape.at(0).engine;
    };
    CHECK(write(true) == write(false));
  }
  for (unsigned p = 0; p < g.nprods; ++p)
    for (unsigned q = 0; q < g.nprods; ++q) {
      lr_reduce_key(g, p, objectKey);
      lr_reduce_key(g, q, arrayKey);
      if (objectKey != arrayKey) continue;
      const auto& x = plans.reductions[p];
      const auto& y = plans.reductions[q];
      CHECK(x.count == y.count);
      for (unsigned k = 0; k < x.count; ++k)
        CHECK(plans.reduceNames[x.names + k] == plans.reduceNames[y.names + k]);
    }
}
}  // namespace fold_contract

void run() {
  fold_contract::precomposition_keeps_bare_records_through_renames();
  fold_contract::substituted_reductions_keep_the_returned_row();
  lexical_derived::owed_terminals_stop_at_the_first_acceptance();
  lexical_derived::relay_chains_share_suffixes_in_both_pool_forms();
  action_rows::run();
  close_rows::run();
  programs::run();
  ordered_reduce::run();
  stamp_layout::run();
  lower_limits::run();
}

}  // namespace lr_construction_cases
