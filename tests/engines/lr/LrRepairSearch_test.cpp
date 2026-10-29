#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "ambtail/generated/G/Parser.hpp"
#include "ambtail/generated/G/Table.hpp"
#include "arithmetics/generated/Arith/Module.hpp"
#include "arithmetics/generated/Arith/Parser.hpp"
#include "arithmetics/generated/Arith/Table.hpp"
#include "asi/generated/G/Parser.hpp"
#include "asi/generated/G/Table.hpp"
#include "asicases/generated/G/Table.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/lr/runtime/LrConfig.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/LrSearch.hpp"
#include "cursiv/engines/lr/runtime/lr_lookahead.hpp"
#include "cursiv/engines/lr/runtime/lr_repair_write.hpp"
#include "cursiv/engines/lr/runtime/lr_search.hpp"
#include "cursiv/engines/lr/runtime/lr_search_bound.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/test/Check.hpp"
#include "election/generated/Election/Parser.hpp"
#include "election/generated/Election/Table.hpp"
#include "domainmodel/generated/DomainModel/Parser.hpp"
#include "domainmodel/generated/DomainModel/Table.hpp"
#include "indent/generated/G/Parser.hpp"
#include "indent/generated/G/Table.hpp"
#include "indentbody/generated/G/Parser.hpp"
#include "indentbody/generated/G/Table.hpp"
#include "astfusedlr/generated/Lr/Parser.hpp"
#include "astfusedlr/generated/Lr/Table.hpp"
#include "nested/generated/G/Table.hpp"
#include "opentoken/generated/G/Parser.hpp"
#include "opentoken/generated/G/Table.hpp"
#include "paritylr/generated/Lr/Table.hpp"
#include "repaircost/generated/G/Parser.hpp"
#include "repaircost/generated/G/Table.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"

// The repair search against what it stands for. Its election is the least key
// an enumeration of every gesture sequence up to a cost finds from a flat
// reading's pause, each goal of the least cost read on to where it is
// refused; the writer it repairs through lays, over a whole valid document and
// through the graphs its cells open, the drive's own records; and the faults
// the old engine repaired by the wrong currency are repaired by the cheaper
// one.

namespace {

namespace ga = ::arith::generated::Arith;
namespace gdm = ::dm::generated::DomainModel;
namespace gi = ::tindent::generated::G;
namespace gt = ::tambtail::generated::G;
namespace gd = ::telect::generated::Election;

// A context standing where a parse of `text` stands before a byte is read.
template <const auto& A>
struct Stand {
  cursiv::Cst cst;
  std::optional<cursiv::CstBuilder> builder;
  std::optional<cursiv::LrContext> c;
  std::vector<cursiv::Scan> room;

  Stand(std::string_view text, const cursiv::LrDrive& drive) {
    cst.reset(text);
    builder.emplace(cst);
    builder->reserve_stream(cst.text().size());
    c.emplace(cst.text(), *builder);
    room.resize(A.maxCands != 0 ? A.maxCands : 1);
    c->cand = room.data();
    c->drive = &drive;
    cursiv::LrParser<A>::install_hooks(*c);
    c->stack.push(
        cursiv::LrFrame{0, cursiv::kNoElement, 0, 0, cursiv::kLrNoWord});
  }
};

constexpr cursiv::RepairBudget kUnbounded{.floor = ~std::uint64_t{0},
                                          .perByte = 0,
                                          .perSearch = ~std::uint64_t{0},
                                          .perParse = ~std::uint64_t{0}};

cursiv::CancellationTokenSource* stopAfterPause = nullptr;
cursiv::NodeCount recordsBeforeStop = 0;

cursiv::LrExit pause_then_cancel(cursiv::LrContext& c) {
  const auto exit = gdm::kDrive.enter(c);
  CHECK(exit == cursiv::LrExit::pause);
  recordsBeforeStop = c.tape.len();
  stopAfterPause->request_stop();
  return exit;
}

// The drive wrote real records before the stop. Recovery must throw rather
// than report a syntax refusal, and the direct caller keeps no partial tape.
void a_stop_at_recovery_discards_the_partial_tree() {
  cursiv::CancellationTokenSource source;
  const auto token = source.get_token();
  stopAfterPause = &source;
  recordsBeforeStop = 0;
  cursiv::LrDrive drive = gdm::kDrive;
  drive.enter = &pause_then_cancel;
  cursiv::Cst cst;
  bool cancelled = false;
  CHECK(!token.stop_requested());
  try {
    (void)cursiv::lr_parser<gdm::kTable>.parse("package p {", cst, nullptr,
                                               &drive, kUnbounded, token);
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  stopAfterPause = nullptr;
  CHECK(cancelled && recordsBeforeStop != 0);
  CHECK(cst.text().empty() && cst.size() == 0 && !cst.accepted() &&
        !cst.has_entry() && cst.diagnostics().empty());
}

// A point in front of the bytes it refuses may do what the phase it stands in
// allows, and a generator names the one reading it was made from and the one
// meaning of its payload.
void phases_and_generators_name_what_they_stand_for() {
  using cursiv::LrRepairPhase;
  using cursiv::LrWorkKind;
  cursiv::LrConfig reading;
  CHECK(!reading.generates() && reading.work == LrWorkKind::read);
  struct Allowed {
    LrRepairPhase phase;
    bool abandon, replace, insert, insertBeforeGarbage;
  };
  for (const Allowed& a :
       {Allowed{LrRepairPhase::beforeGesture, true, true, true, true},
        Allowed{LrRepairPhase::afterAbandon, false, true, true, true},
        Allowed{LrRepairPhase::inserting, false, true, true, false},
        Allowed{LrRepairPhase::discardOnly, false, false, false, false}}) {
    reading.phase = a.phase;
    CHECK(reading.mayAbandon() == a.abandon);
    CHECK(reading.mayReplace() == a.replace);
    CHECK(reading.mayInsert(false) == a.insert);
    CHECK(reading.mayInsert(true) == a.insertBeforeGarbage);
  }

  const auto inserts = cursiv::LrConfig::insertGenerator(reading, 3, 7);
  CHECK(inserts.generates() && inserts.work == LrWorkKind::enumerateInsertions);
  CHECK(inserts.source() == 3 && inserts.insertRank() == 7);
  auto abandons = cursiv::LrConfig::abandonGenerator(reading, 2, 5, 11, 13, 4);
  CHECK(abandons.generates() && abandons.work == LrWorkKind::enumerateAbandons);
  CHECK(abandons.source() == 2 && abandons.abandonDepth() == 5 &&
        abandons.abandonNode() == 11 && abandons.abandonBegin() == 13 &&
        abandons.armNext == 4);
  abandons.resumeAbandons(4, 9, 10, 6);
  CHECK(abandons.source() == 2 && abandons.abandonDepth() == 4 &&
        abandons.abandonNode() == 9 && abandons.abandonBegin() == 10 &&
        abandons.armNext == 6);
}

// Equal stacks at the same pause can still owe different insertion costs:
// their allowances were fixed before the reductions that brought them here.
// Both stand at an equal key; at a worse key, only one holding more.
void insertion_allowance_is_part_of_the_future() {
  Stand<gdm::kTable> st("package p { package q {", gdm::kDrive);
  cursiv::LrContext& c = *st.c;
  CHECK(gdm::kDrive.enter(c) == cursiv::LrExit::pause);
  cursiv::LrSearch s(c.len, kUnbounded, c.indentArena);
  cursiv::lr_search_start(c, s);
  s.configs[0].fresh = false;
  s.configs[0].phase = cursiv::LrRepairPhase::inserting;
  auto free = s.configs[0];
  free.freeInserts = 2;
  free.link = cursiv::lr_search_link(s, cursiv::LrGesture{.choice = 1});
  s.configs.push_back(free);
  for (const std::uint64_t held : {1u, 3u}) {
    auto dear = free;
    dear.cost += cursiv::kRepairGesture;
    dear.freeInserts = held;
    s.configs.push_back(dear);
  }
  cursiv::lr_search_admit(s, 1);
  cursiv::lr_search_admit(s, 0);
  CHECK(!s.configs[0].gone && !s.configs[1].gone);
  cursiv::lr_search_admit(s, 2);
  cursiv::lr_search_admit(s, 3);
  CHECK(s.configs[2].gone && !s.configs[3].gone);

  auto owed = gdm::kTable.cold.close_row(c.stack.top().state);
  const auto term = static_cast<cursiv::SymbolId>(owed.take());
  CHECK(term < gdm::kTable.cfg.nterms);
  for (unsigned i = 0; i < 2; ++i) {
    bool due = false;
    cursiv::lr_search_insert<gdm::kTable>(c, s, i, term, 0, due);
    CHECK(due && !s.out.empty());
    for (const auto q : s.out) {
      CHECK(s.configs[q].cost == (i == 0 ? cursiv::kRepairGesture : 0));
      if (i == 1) CHECK(s.configs[q].freeInserts == 1);
    }
  }
}

// In front of bytes no mode reads, one insertion is made, never a run: each
// reading refuses them alike.
void unread_bytes_take_one_insertion() {
  Stand<gdm::kTable> st("entity E { \xc3\xa9 : T }", gdm::kDrive);
  cursiv::LrContext& c = *st.c;
  CHECK(gdm::kDrive.enter(c) == cursiv::LrExit::pause);
  CHECK(c.la.sym == cursiv::kInvalidSymbol);
  cursiv::LrSearch s(c.len, kUnbounded, c.indentArena);
  cursiv::lr_search_start(c, s);
  cursiv::lr_search_expand<gdm::kTable>(c, s, 0);
  std::uint32_t one = cursiv::kLrNoConfig;
  for (std::uint32_t i = 1; i < s.configs.size(); ++i)
    if (!s.configs[i].generates() &&
        s.configs[i].phase == cursiv::LrRepairPhase::inserting &&
        s.configs[i].la.begin == c.la.begin)
      one = i;
  CHECK(one != cursiv::kLrNoConfig);
  if (one == cursiv::kLrNoConfig) return;
  const std::size_t laid = s.links.size();
  cursiv::lr_search_expand<gdm::kTable>(c, s, one);
  bool again = false, dropped = false;
  for (std::size_t l = laid; l < s.links.size(); ++l) {
    again = again || s.links[l].kind == cursiv::LrGesture::ins;
    dropped = dropped || s.links[l].kind == cursiv::LrGesture::del;
  }
  CHECK(!again && dropped);
}

// A merged reading follows the better-ranked path, including the source it
// set out from. At the accepted reading's cost, that source decides whether
// the surviving goal may still outrank it.
void merged_path_keeps_its_source_rank() {
  Stand<gdm::kTable> st("package p {", gdm::kDrive);
  cursiv::LrContext& c = *st.c;
  CHECK(gdm::kDrive.enter(c) == cursiv::LrExit::pause);
  cursiv::LrSearch s(c.len, kUnbounded, c.indentArena);
  cursiv::lr_search_start(c, s);
  cursiv::lr_search_root(s, c, c.committed, c.stack, c.tape.len(), 1, true);
  s.heap.clear();
  auto owed = gdm::kTable.cold.close_row(c.stack.top().state);
  const auto term = static_cast<cursiv::SymbolId>(owed.take());
  CHECK(term < gdm::kTable.cfg.nterms);
  std::uint32_t first = cursiv::kLrNoConfig, last = cursiv::kLrNoConfig;
  for (const std::uint32_t source : {1u, 0u}) {
    bool due = false;
    cursiv::lr_search_insert<gdm::kTable>(c, s, source, term, 0, due);
    CHECK(due && s.out.size() == 1);
    const auto goal = s.out[0];
    CHECK(s.configs[goal].done);
    cursiv::lr_search_admit(s, goal);
    if (first == cursiv::kLrNoConfig) first = goal;
    last = goal;
  }
  CHECK(!s.configs[first].gone && s.configs[last].gone);
  CHECK(cursiv::lr_search_root_of(s.links, s.configs[first].link) == 0);
  bool exhausted = false, raced = false;
  CHECK(cursiv::lr_search_run<gdm::kTable>(c, s, exhausted, raced) == first);
  CHECK(!exhausted && !raced);
}

// The order a search elects in: cost, how far the goal reads on -- further
// first -- then the author's tokens lost, faults, tokens invented, and the
// order of the frozen reading it set out from.
using Key = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t,
                       std::uint32_t, std::uint32_t, std::uint64_t>;
constexpr Key kNoKey{~0u, ~0u, ~0u, ~0u, ~0u, ~std::uint64_t{0}};

Key key_of(const cursiv::LrConfig& x, cursiv::TextOffset reach,
           std::uint64_t order) {
  return {x.cost, ~reach, x.lost, x.faults, x.invented, order};
}

// The order of the frozen reading a path set out from.
using Rank = std::function<std::uint64_t(const cursiv::LrSearch&,
                                         std::uint32_t)>;

// Goal `g` read on with no gesture, every arm of every cell: where its
// furthest reading is refused, or all of it where one accepts.
template <const auto& A>
cursiv::TextOffset reach_of(cursiv::LrContext& c, cursiv::LrSearch& s,
                            std::uint32_t g) {
  if (s.configs[g].done) return cursiv::kLrReachAll;
  s.configs.push_back(s.configs[g]);
  std::vector<std::uint32_t> todo{
      static_cast<std::uint32_t>(s.configs.size()) - 1};
  cursiv::TextOffset far = 0;
  cursiv::LrSearchPool<A> x{c, s};
  while (!todo.empty()) {
    const std::uint32_t i = todo.back();
    todo.pop_back();
    for (;;) {
      if (s.configs[i].done) return cursiv::kLrReachAll;
      if (s.configs[i].gone) {
        far = std::max(far, s.configs[i].la.begin);
        break;
      }
      s.reading.clear();
      s.configs[i].dead = false;
      s.configs[i].armNext = 0;
      cursiv::lr_read_step<A>(c, x, i);
      todo.insert(todo.end(), s.reading.begin(), s.reading.end());
      if (s.configs[i].dead) {
        far = std::max(far, s.configs[i].la.begin);
        break;
      }
    }
  }
  return far;
}

// Each of `goals` keyed as it reads on: as far as the furthest of those
// that make its gestures, the arms they took aside.
template <const auto& A>
std::vector<Key> keys_of(cursiv::LrContext& c, cursiv::LrSearch& s,
                         const std::vector<std::uint32_t>& goals,
                         const Rank& rank) {
  std::vector<cursiv::TextOffset> far(goals.size());
  for (std::size_t k = 0; k < goals.size(); ++k)
    far[k] = reach_of<A>(c, s, goals[k]);
  std::vector<Key> out;
  for (std::size_t k = 0; k < goals.size(); ++k) {
    cursiv::TextOffset group = far[k];
    for (std::size_t j = 0; j < goals.size(); ++j)
      if (cursiv::lr_same_gestures(s.links, s.configs[goals[k]].link,
                                   s.configs[goals[j]].link))
        group = std::max(group, far[j]);
    out.push_back(key_of(s.configs[goals[k]], group,
                         rank(s, s.configs[goals[k]].link)));
  }
  return out;
}

// Every gesture sequence from the roots `lay` sets out, to cost `most`, each
// reading kept whatever another with its future holds: the least key a goal
// reaches. Every refused reading on
// the way is bounded by what its readings below reach: no goal under it is
// cheaper than its bound says, paid then lost.
template <const auto& A, class Lay>
Key enumerate(cursiv::LrContext& c, std::uint32_t most, std::string_view text,
              Lay lay, const Rank& rank) {
  cursiv::LrSearch s(c.len, kUnbounded, c.indentArena);
  if (!lay(s)) return kNoKey;
  std::vector<cursiv::LrSearch::Entry> todo(s.heap);
  s.heap.clear();
  std::vector<std::uint32_t> goals;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> refused;
  while (!todo.empty()) {
    const cursiv::LrSearch::Entry e = todo.back();
    todo.pop_back();
    const cursiv::LrConfig& x = s.configs[e.at];
    if (x.gone || x.cost > most) continue;
    s.seen.clear();
    if (x.work == cursiv::LrWorkKind::enumerateInsertions) {
      cursiv::lr_search_inserts<A>(c, s, e.at);
    } else if (x.work == cursiv::LrWorkKind::enumerateAbandons) {
      cursiv::lr_search_abandon<A>(c, s, e.at, ~std::uint32_t{0});
    } else if (cursiv::lr_search_goal(x)) {
      goals.push_back(e.at);
    } else {
      refused.emplace_back(e.at, cursiv::lr_search_bound<A>(c, s, x));
      cursiv::lr_search_expand<A>(c, s, e.at);
    }
    todo.insert(todo.end(), s.heap.begin(), s.heap.end());
    s.heap.clear();
  }
  std::vector<std::uint32_t> owner(s.links.size(), cursiv::kLrNoConfig);
  for (std::uint32_t i = 0; i < s.configs.size(); ++i)
    if (!s.configs[i].generates() &&
        owner[s.configs[i].link] == cursiv::kLrNoConfig)
      owner[s.configs[i].link] = i;
  std::vector<std::uint64_t> below(s.configs.size(), ~std::uint64_t{0});
  for (const cursiv::LrConfig& g : s.configs) {
    if (g.generates() || g.gone || !cursiv::lr_search_goal(g)) continue;
    const std::uint64_t k = (std::uint64_t{g.cost} << 32) | g.lost;
    for (std::uint32_t l = g.link; l != cursiv::kNoGestureLink;
         l = s.links[l].parent)
      if (owner[l] != cursiv::kLrNoConfig)
        below[owner[l]] = std::min(below[owner[l]], k);
  }
  for (const auto& [i, owed] : refused) {
    const cursiv::LrConfig& x = s.configs[i];
    const std::uint64_t floor =
        (std::uint64_t{x.cost + owed / cursiv::kLrPaid} << 32) |
        (x.lost + owed % cursiv::kLrPaid);
    if (below[i] == ~std::uint64_t{0} || floor <= below[i]) continue;
    std::printf("FAIL: `%.*s` bounds a reading at %llx, a goal under it %llx\n",
                static_cast<int>(text.size()), text.data(),
                static_cast<unsigned long long>(floor),
                static_cast<unsigned long long>(below[i]));
    ++failures;
  }
  std::erase_if(goals, [&](std::uint32_t g) { return s.configs[g].gone; });
  std::uint32_t cheapest = ~0u;
  for (const std::uint32_t g : goals)
    cheapest = std::min(cheapest, s.configs[g].cost);
  std::erase_if(goals,
                [&](std::uint32_t g) { return s.configs[g].cost != cheapest; });
  const std::vector<Key> keys = keys_of<A>(c, s, goals, rank);
  return keys.empty() ? kNoKey : *std::ranges::min_element(keys);
}

void print_key(const Key& k) {
  const auto [cost, far, lost, faults, invented, order] = k;
  std::printf(
      "(cost %u, reach %u, lost %u, faults %u, invented %u, order %llu)", cost,
      ~far, lost, faults, invented, static_cast<unsigned long long>(order));
}

// The search's election from the roots `lay` sets out, keyed as the
// enumeration keys it.
template <const auto& A, class Lay>
Key elect(cursiv::LrContext& c, Lay lay, const Rank& rank,
          std::uint32_t& cost) {
  cursiv::LrSearch s(c.len, kUnbounded, c.indentArena);
  s.boundAfter = 0;
  cost = ~0u;
  if (!lay(s)) return kNoKey;
  bool exhausted = false;
  bool raced = false;
  const std::uint32_t at = cursiv::lr_search_run<A>(c, s, exhausted, raced);
  if (at == cursiv::kLrNoConfig) return kNoKey;
  cost = s.configs[at].cost;
  const auto won = std::ranges::find(s.goals, at);
  return won == s.goals.end()
             ? key_of(s.configs[at], 0, rank(s, s.configs[at].link))
             : keys_of<A>(c, s, s.goals, rank)[won - s.goals.begin()];
}

// The search elects the least key the enumeration finds from the same roots,
// where it finds one within the cost it enumerates to.
template <const auto& A, class Lay>
void agree(cursiv::LrContext& c, std::string_view text, Lay lay,
           const Rank& rank) {
  constexpr std::uint32_t kMost = 3 * cursiv::kRepairGesture;
  std::uint32_t cost = 0;
  const Key elected = elect<A>(c, lay, rank, cost);
  const Key least = enumerate<A>(c, kMost, text, lay, rank);
  if (cost <= kMost ? elected == least : least == kNoKey) return;
  std::printf("FAIL: `%.*s` elects ", static_cast<int>(text.size()),
              text.data());
  print_key(elected);
  std::printf(", the enumeration ");
  print_key(least);
  std::printf("\n");
  ++failures;
}

// From the pause a flat reading stands in, the search elects what the
// enumeration finds.
template <const auto& A>
void exact(const cursiv::LrDrive& drive, std::string_view text) {
  Stand<A> st(text, drive);
  cursiv::LrContext& c = *st.c;
  if (drive.enter(c) != cursiv::LrExit::pause) {
    std::printf("FAIL: `%.*s` does not pause\n", static_cast<int>(text.size()),
                text.data());
    ++failures;
    return;
  }
  agree<A>(
      c, text,
      [&](cursiv::LrSearch& s) {
        cursiv::lr_search_start(c, s);
        return true;
      },
      [](const cursiv::LrSearch&, std::uint32_t) { return 0u; });
}

// The writer's pool over a whole document: the lexing is the drive's --
// trivia laid, the cursor past it -- and a cell is decided as the drive
// decides it.
template <const auto& A>
struct Whole {
  cursiv::LrContext& c;
  cursiv::LrWriteReading& w;
  std::uint32_t forks = 0;
  bool split = false;
  cursiv::LrWriteReading scratch{};

  cursiv::LrWriteReading& operator[](std::size_t i) {
    return i == 0 ? w : scratch;
  }
  std::size_t fork(std::size_t) {
    ++forks;
    return 1;
  }
  void drop(std::size_t) {}
  bool apply(std::size_t i, std::uint32_t at) {
    if (i != 0) return true;
    if (forks != 0) {
      const unsigned arm =
          c.resolveAt(c, c.stack.top().state, at, forks + 1);
      split = split || arm > forks;
      if (arm > forks) return false;
      at += arm;
      forks = 0;
    }
    return cursiv::lr_write_apply<A>(c, w, at);
  }
  bool reduce(std::size_t, std::uint16_t p) {
    return cursiv::lr_write_reduce<A>(c, p, cursiv::lr_shadow_holds(c, w));
  }
  cursiv::Token scan(std::size_t, std::uint16_t mode) {
    c.pending.clear();
    cursiv::TextOffset p = c.committed;
    for (;;) {
      while (p < c.len && cursiv::lr_skips<A>(
                              mode, static_cast<unsigned char>(c.src[p])))
        ++p;
      c.pendEnd = p;
      if (p >= c.len) return cursiv::Token{p, p, cursiv::kEofSymbol};
      const cursiv::Scan s = c.drive->walk(c.src, c.len, mode, p);
      if (s.term == cursiv::kInvalidSymbol || s.end == p)
        return cursiv::Token{p, p, cursiv::kInvalidSymbol};
      if (!cursiv::lr_is_trivia<A>(s.term) &&
          !cursiv::lr_joins<A>(w.indent.depth, s.term))
        return cursiv::Token{p, s.end, s.term};
      if (A.cfg.terms[s.term].recorded)
        c.pending.push_back(cursiv::Token{p, s.end, s.term});
      p = s.end;
    }
  }
  void lexed() {}
  void kept_at(std::size_t) {}
  void cropped() {}
  void stepped() {}
};

// A valid document read whole by the interpreted writer lays the records the
// drive lays. False where a cell split rather than answered: the drive's
// branches are not the writer's to follow.
template <const auto& A>
bool writes_as_drive(const cursiv::LrDrive& drive, std::string_view text,
                     const char* name) {
  Stand<A> ran(text, drive);
  if (drive.enter(*ran.c) != cursiv::LrExit::accept) return false;
  Stand<A> read(text, drive);
  cursiv::LrContext& c = *read.c;
  cursiv::LrWriteReading w;
  w.run.s = &c.stack;
  Whole<A> x{c, w};
  while (!w.done) {
    w.dead = false;
    cursiv::lr_read_step<A>(c, x, 0);
    if (x.split) return false;
    if (w.dead) {
      std::printf("FAIL: %s: the writer refuses a valid document at %u\n",
                  name, w.la.begin);
      ++failures;
      return true;
    }
  }
  cursiv::lr_flush_trivia<A>(c);
  const cursiv::CstRun& a = ran.builder->stream();
  const cursiv::CstRun& b = read.builder->stream();
  if (a.records() != b.records()) {
    std::printf("FAIL: %s: %u records written, the drive's %u\n", name,
                b.records(), a.records());
    ++failures;
    return true;
  }
  for (cursiv::NodeCount i = 0; i < a.records(); ++i) {
    const cursiv::CstNode& x0 = a.at(i);
    const cursiv::CstNode& y0 = b.at(i);
    if (x0.begin == y0.begin && x0.end == y0.end &&
        x0.subtreeSize == y0.subtreeSize && x0.engine == y0.engine &&
        x0.hidden == y0.hidden && x0.recovered == y0.recovered)
      continue;
    std::printf("FAIL: %s: record %u differs from the drive's\n", name, i);
    ++failures;
    return true;
  }
  return true;
}

// The drive read off the table: the interpreted writer takes the actions the
// generated states take, lexes as they lex, and decides a cell and a position
// of several candidates as they decide them -- stopping where they stop for
// the graph.
template <const auto& A>
struct Interpreted {
  static inline cursiv::LrDrive drive{};
  // The positions of several candidates the writer decided as the drive does.
  [[maybe_unused]] static inline unsigned picks = 0;

  struct Pool : Whole<A> {
    void sync() {
      this->c.la = this->w.la;
      this->c.laMode = this->w.laMode;
      this->c.virt = this->w.virt;
      this->c.indent = this->w.indent;
    }
    bool apply(std::size_t i, std::uint32_t at) {
      if (i != 0) return true;
      if (this->forks != 0) {
        sync();
        const unsigned arm = this->c.resolveAt(
            this->c, this->c.stack.top().state, at, this->forks + 1);
        // Past every arm, the drive's switch stops: the graph reads the cell.
        if (arm > this->forks) {
          this->forks = 0;
          return false;
        }
        at += arm;
        this->forks = 0;
      }
      return cursiv::lr_write_apply<A>(this->c, this->w, at);
    }
    void pick(std::size_t, std::uint32_t state, cursiv::Scan* room,
              std::uint32_t n) {
      ++Interpreted::picks;
      const cursiv::Scan best = room[0];
      std::uint32_t kept = 0;
      for (std::uint32_t k = 0; k < n; ++k)
        if (A.cold.follow[state].test(room[k].term)) room[kept++] = room[k];
      if (kept == 0) {
        this->w.la = cursiv::Token{this->w.la.begin, best.end, best.term};
        return;
      }
      sync();
      std::vector<cursiv::Scan> arms(room, room + kept);
      const std::uint32_t k =
          kept > 1 ? this->c.candAt(this->c, state, arms.data(), kept) : 0;
      if (k >= kept) {
        this->w.la = cursiv::Token{this->w.la.begin, this->w.la.begin,
                                   cursiv::kInvalidSymbol};
        return;
      }
      this->w.la = cursiv::Token{this->w.la.begin, arms[k].end, arms[k].term};
    }
  };

  static cursiv::LrExit enter(cursiv::LrContext& c) {
    cursiv::LrWriteReading w = cursiv::lr_write_reading(c);
    Pool x{{c, w}};
    for (;;) {
      w.dead = false;
      cursiv::lr_read_step<A>(c, x, 0);
      x.sync();
      if (w.dead) return cursiv::LrExit::pause;
      if (w.done) {
        cursiv::lr_flush_trivia<A>(c);
        return cursiv::LrExit::accept;
      }
    }
  }
};

// A valid document parsed twice, by the drive and by the interpreted
// writer, graphs and all: the records are the same. The graphs the drive's
// parse opened, counted.
template <const auto& A>
unsigned writes_through(const cursiv::LrDrive& drive, std::string_view text,
                        const char* name) {
  Interpreted<A>::drive = drive;
  Interpreted<A>::drive.enter = &Interpreted<A>::enter;
  cursiv::Cst ran;
  cursiv::LrParseStats stats{};
  const bool ok = cursiv::lr_parser<A>.parse(text, ran, &stats, &drive);
  cursiv::Cst read;
  const bool same =
      cursiv::lr_parser<A>.parse(text, read, nullptr, &Interpreted<A>::drive);
  if (!ok || !same || !ran.diagnostics().empty() ||
      !read.diagnostics().empty()) {
    std::printf("FAIL: %s: accepted %d by the drive, %d interpreted\n", name,
                ok ? 1 : 0, same ? 1 : 0);
    ++failures;
    return stats.splits;
  }
  if (ran.size() != read.size()) {
    std::printf("FAIL: %s: %u records interpreted, the drive's %u\n", name,
                read.size(), ran.size());
    ++failures;
    return stats.splits;
  }
  for (cursiv::NodeCount i = 0; i < ran.size(); ++i) {
    const cursiv::CstNode& x0 = ran[i];
    const cursiv::CstNode& y0 = read[i];
    if (x0.begin == y0.begin && x0.end == y0.end &&
        x0.subtreeSize == y0.subtreeSize && x0.engine == y0.engine &&
        x0.hidden == y0.hidden && x0.recovered == y0.recovered)
      continue;
    std::printf("FAIL: %s: record %u differs from the drive's\n", name, i);
    ++failures;
    return stats.splits;
  }
  return stats.splits;
}

// A document repaired twice, by the drive and by the interpreted writer: the
// two pause at the same tokens, so their repairs say and spend the same.
template <const auto& A>
void repairs_through(const cursiv::LrDrive& drive, std::string_view text) {
  Interpreted<A>::drive = drive;
  Interpreted<A>::drive.enter = &Interpreted<A>::enter;
  cursiv::Cst ran;
  cursiv::Cst read;
  cursiv::LrParseStats ranStats{};
  cursiv::LrParseStats readStats{};
  CHECK(cursiv::lr_parser<A>.parse(text, ran, &ranStats, &drive));
  CHECK(cursiv::lr_parser<A>.parse(text, read, &readStats,
                                   &Interpreted<A>::drive));
  CHECK(ranStats.work == readStats.work);
  CHECK(ran.diagnostics().size() == read.diagnostics().size());
  CHECK(ran.size() == read.size());
  for (cursiv::NodeId i = 0; i < ran.size() && i < read.size(); ++i)
    CHECK(ran[i].begin == read[i].begin && ran[i].end == read[i].end &&
          ran[i].subtreeSize == read[i].subtreeSize &&
          ran[i].recovered == read[i].recovered);
}

template <const auto& A>
std::vector<cursiv::ParseDiagnostic> repaired(
    std::string_view text, const cursiv::LrDrive& drive,
    const cursiv::RepairBudget& budget = {}) {
  cursiv::Cst cst;
  (void)cursiv::lr_parser<A>.parse(text, cst, nullptr, &drive, budget);
  const auto d = cst.diagnostics();
  return {d.begin(), d.end()};
}

bool says(const std::vector<cursiv::ParseDiagnostic>& d,
          cursiv::DiagnosticKind kind) {
  return std::ranges::any_of(
      d, [&](const cursiv::ParseDiagnostic& x) { return x.kind == kind; });
}

std::vector<cursiv::ParseDiagnostic> repaired_indent(std::string_view text) {
  cursiv::Cst cst;
  (void)cursiv::lr_parser<gi::kTable>.parse(text, cst, nullptr, &gi::kDrive);
  const auto d = cst.diagnostics();
  return {d.begin(), d.end()};
}

}  // namespace

// Which repair a reading makes, and what it costs over a long document.

// Which repair a reading makes, and how much the search opens to find it.
namespace repair_choice {

namespace arith_g = arith::generated::Arith;
namespace indent_g = tindent::generated::G;
namespace memconv_g = tfused::generated::Lr;
namespace open_g = topen::generated::G;

namespace {

// A missing `;` is owed where the whole definition closes, many rungs above
// the operand the parse froze on: every rung of the climb offers its own
// insertions, and the one the next line takes wins.
void a_missing_terminator_is_inserted_where_the_construct_closes() {
  const auto inserted = [](const std::string& text) {
    cursiv::Cst cst;
    const bool ok = cursiv::lr_parser<arith_g::kTable>.parse(
        text, cst, nullptr, &arith_g::kDrive);
    CHECK(ok);
    unsigned n = 0;
    for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
      n += d.kind == cursiv::DiagnosticKind::inserted ? 1u : 0u;
    CHECK(n == cst.diagnostics().size());
    return n;
  };
  CHECK(inserted("module m\ndef x: 1\ndef y: 2;\n") == 1);
  std::string body = "module m\n";
  for (unsigned i = 0; i < 5; ++i)
    body += "def x" + std::to_string(i) + "(a): a * (a + " + std::to_string(i) +
            ")\n";
  CHECK(inserted(body + "def z: 0;\n") == 5);
}

// A line indented deeper than a statement allows is one fault: the level
// its skipped indent opened closes with no second word, whether its dedent
// comes alone or after a suite's own.
void a_stray_indent_is_said_once() {
  for (const char* text :
       {"a\n  b\nc\n", "a:\n  b\n    c\nd\n", "a\n  b\n  c\n  d\ne\n"}) {
    cursiv::Cst cst;
    const bool ok = cursiv::lr_parser<indent_g::kTable>.parse(
        text, cst, nullptr, &indent_g::kDrive);
    CHECK(ok);
    CHECK(cst.diagnostics().size() == 1);
  }
}

// A word a guard refuses, ending the document: the walk that refused it is
// still alive at the end, on a path no token ends on, so the word is not a
// token left open -- it is read as the word it misspells, not dropped whole.
void a_refused_word_at_the_end_is_not_an_open_token() {
  for (const char* text : {"abcd", "abcde"}) {
    cursiv::Cst cst;
    const bool ok = cursiv::lr_parser<open_g::kTable>.parse(text, cst, nullptr,
                                                            &open_g::kDrive);
    CHECK(ok);
    CHECK(cst.diagnostics().size() == 1);
    for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
      CHECK(d.kind == cursiv::DiagnosticKind::replaced && d.begin == 0 &&
            d.end == std::string_view(text).size());
  }
}

// A token refused in its middle ends where its own closer stands, however
// much inside it is wrong or how far the closer is: one re-reading, and the
// text after it read as written.
void a_token_refused_in_its_middle_ends_at_its_closer() {
  const auto one = [&](const std::string& text, cursiv::TextOffset begin,
                       cursiv::TextOffset end) {
    cursiv::Cst cst;
    CHECK(cursiv::lr_parser<memconv_g::kTable>.parse(text, cst, nullptr,
                                                     &memconv_g::kDrive));
    CHECK(cst.diagnostics().size() == 1);
    for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
      CHECK(d.kind == cursiv::DiagnosticKind::replaced && d.begin == begin &&
            d.end == end);
  };
  one("'ab@d' 'e' 'f'", 0, 6);
  one("'a' 'bc@de' 'f'", 4, 11);
  for (const std::size_t n : {10u, 250u, 5000u}) {
    const std::string s =
        "'" + std::string(10, 'a') + "1" + std::string(n, 'a') + "'";
    one("'b' " + s + " 'c'", 4, static_cast<cursiv::TextOffset>(4 + s.size()));
  }
}

// A comment that never closed ends at the first line after which the text
// reads on as code: one fault, and the code after it kept.
void an_open_comment_ends_where_the_code_resumes() {
  for (const unsigned lines : {1u, 2u, 4u}) {
    std::string comment = "/* a note";
    for (unsigned k = 1; k < lines; ++k) comment += "\n   that goes on";
    const std::string text =
        "module m\ndef f(x):\n  " + comment + "\n  x * 2;\nf(1);\n";
    cursiv::Cst cst;
    CHECK(cursiv::lr_parser<arith_g::kTable>.parse(text, cst, nullptr,
                                                   &arith_g::kDrive));
    CHECK(cst.diagnostics().size() == 1);
    const auto at = static_cast<cursiv::TextOffset>(text.find("/*"));
    for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
      CHECK(d.kind == cursiv::DiagnosticKind::replaced && d.begin == at &&
            d.end == at + comment.size());
  }
}

// A deletion names the token the reading resumes on: what the author is
// shown next to the text that went.
void a_deletion_names_the_token_it_resumes_on() {
  const auto parser = arith_g::create_parser();
  const cursiv::ParseResult r = parser->parse("module m f(1 ?);");
  CHECK(r.diagnostics.size() == 1);
  for (const cursiv::ParseDiagnostic& d : r.diagnostics) {
    CHECK(d.kind == cursiv::DiagnosticKind::deleted && d.begin == 13 &&
          d.end == 14);
    const auto spellings = parser->getGrammar().terminalSpellings(d.element);
    CHECK(spellings.size() == 1 && spellings.front() == ")");
    CHECK(d.also.empty());
  }
}

}  // namespace

void run() {
  a_missing_terminator_is_inserted_where_the_construct_closes();
  a_stray_indent_is_said_once();
  a_refused_word_at_the_end_is_not_an_open_token();
  a_deletion_names_the_token_it_resumes_on();
  a_token_refused_in_its_middle_ends_at_its_closer();
  an_open_comment_ends_where_the_code_resumes();
}

}  // namespace repair_choice

// A repair costs what the document holds, never its square: each case is a
// document of one fault repeated, long enough that a quadratic repair breaks
// the lane's timeout before any check below is read.
namespace repair_scale {

namespace arith_g = arith::generated::Arith;
namespace asi_g = tasi::generated::G;
namespace dm_g = dm::generated::DomainModel;

namespace {

struct Read {
  bool accepted = false;
  std::size_t diagnostics = 0;
  cursiv::LrParseStats stats;
};

template <const auto& A>
Read read(const std::string& text, const cursiv::LrDrive* drive) {
  cursiv::Cst cst;
  Read r;
  r.accepted = cursiv::lr_parser<A>.parse(text, cst, &r.stats, drive);
  r.diagnostics = cst.diagnostics().size();
  return r;
}

// `=` for `:` in every definition: one note each up to the fault the parse's
// ceiling on repair work stops on, then one `bounded` note from there to the
// end.
void a_fault_per_member_is_said_once_each() {
  constexpr unsigned kKeys = 80000;
  std::string text = "module m";
  for (unsigned i = 0; i < kKeys; ++i)
    text += "\ndef k" + std::to_string(i) + " = 0;";
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<arith_g::kTable>.parse(text, cst, &stats,
                                                 &arith_g::kDrive));
  const auto& d = cst.diagnostics();
  const auto bounded = std::ranges::find(d, cursiv::DiagnosticKind::bounded,
                                         &cursiv::ParseDiagnostic::kind);
  CHECK(stats.budgetStops == 1 && bounded != d.end() &&
        bounded->end == text.size());
  if (bounded == d.end()) return;
  const auto said = std::ranges::count(d, cursiv::DiagnosticKind::replaced,
                                       &cursiv::ParseDiagnostic::kind);
  CHECK(static_cast<std::size_t>(said) + 1 == d.size());
  CHECK(said == std::count(text.begin(), text.begin() + bounded->begin + 1,
                           '='));
}

// Calls left open to the end of the file: each is closed one way or the
// other, and the readings frozen alike merge instead of doubling per call.
void readings_frozen_alike_do_not_fill_the_room() {
  std::string text;
  for (unsigned i = 0; i < 40; ++i) text += "a(";
  text += "b";
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  cursiv::lr_parser<asi_g::kTable>.parse(text, cst, &stats, &asi_g::kDrive);
  CHECK(stats.alternatives < 4096);
}

// A doubled comma after every argument: each fault leaves readings behind
// that the next one outruns, so the episode never closes.
void a_document_of_close_faults_reads_in_one_pass() {
  constexpr unsigned kElements = 4000;
  std::string text = "module m f(1";
  for (unsigned i = 1; i < kElements; ++i) text += ",,1";
  text += ");";
  const Read r = read<arith_g::kTable>(text, &arith_g::kDrive);
  CHECK(r.accepted);
  CHECK(r.diagnostics == kElements - 1);
}

// Packages left open to the end of the file: one fault closes them all, and
// each planned step reads only the frames it moved. Read against the whole
// plan's lowest frame, this document takes seconds; each step alone, a few
// hundredths.
void a_run_of_closers_replays_in_one_pass() {
  std::string text;
  for (unsigned i = 0; i < 50000; ++i) text += "package p {";
  const auto start = std::chrono::steady_clock::now();
  const Read r = read<dm_g::kTable>(text, &dm_g::kDrive);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
  CHECK(r.accepted);
  CHECK(r.diagnostics == 1);
}

}  // namespace

void run() {
  a_fault_per_member_is_said_once_each();
  readings_frozen_alike_do_not_fill_the_room();
  a_document_of_close_faults_reads_in_one_pass();
  a_run_of_closers_replays_in_one_pass();
}

}  // namespace repair_scale

// Repairs over long documents: their cost, their ledger, and the arms a
// repaired reading forks.

// A repair's cost follows the document, not the square of it: 16 KiB of
// stray bytes between good tokens.
namespace repair_cost {

namespace g = trepair::generated::G;

namespace {

// `w ?` repeated: every other token is one the grammar cannot place, so each
// search runs on into the next fault until its share answers.
std::string noise(unsigned n) {
  std::string t = "element e { ";
  for (unsigned i = 0; i < n; ++i) t += "w ? ";
  return t + "w } end";
}

void a_document_of_stray_bytes_is_read_in_one_pass() {
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  const std::string text = noise(4096);
  cursiv::lr_parser<g::kTable>.parse(text, cst, &stats, &g::kDrive);
  // What a quadratic repair breaks first is the clock, and the lane's own
  // timeout is what catches that. Each stray byte is said once, within the
  // budget.
  unsigned bounded = 0;
  for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
    bounded += d.kind == cursiv::DiagnosticKind::bounded ? 1u : 0u;
  CHECK(cst.diagnostics().size() == 4096);
  CHECK(bounded == 0);
}

}  // namespace

void run() {
  a_document_of_stray_bytes_is_read_in_one_pass();
}

}  // namespace repair_cost

// A cascade longer than any one ledger: every leaf a repair deleted is still
// under a diagnostic, whatever the document's fault count.
namespace ledger {

namespace g = trepair::generated::G;

namespace {

// One fault, then three items the grammar takes: the reading shifts between
// two faults, so each opens a hole of its own and speaks its own note.
std::string faults(unsigned n) {
  std::string t = "element e { w";
  for (unsigned i = 0; i < n; ++i) t += " ? ; w ; w ; w";
  return t + " } end";
}

void every_deleted_leaf_is_covered(unsigned n) {
  const std::string text = faults(n);
  cursiv::Cst cst;
  cursiv::lr_parser<g::kTable>.parse(text, cst, nullptr, &g::kDrive);
  unsigned deleted = 0;
  unsigned uncovered = 0;
  for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
    const cursiv::CstNode& node = cst[id];
    if (!node.recovered || node.subtreeSize != 1 ||
        g::kTable.stamp.row(node) != cursiv::kNoElement)
      continue;
    ++deleted;
    bool covered = false;
    for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
      if (d.begin <= node.begin && node.end <= d.end) {
        covered = true;
        break;
      }
    if (!covered) ++uncovered;
  }
  CHECK(deleted == n);
  CHECK(uncovered == 0);
}

void a_document_of_faults_is_covered_at_any_length() {
  every_deleted_leaf_is_covered(100);
  every_deleted_leaf_is_covered(9000);
}

}  // namespace

void run() {
  a_document_of_faults_is_covered_at_any_length();
}

}  // namespace ledger

// A repair followed by a tail of statements each crossing a cell the text
// never settles: the arms a repaired reading forks must merge back like any
// other's, or they double at every statement.
namespace ambiguous_tail {

namespace g = tambtail::generated::G;

namespace {

cursiv::LrParseStats parse(const std::string& text, bool& accepted,
                           std::size_t& repairs) {
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  accepted =
      cursiv::lr_parser<g::kTable>.parse(text, cst, &stats, &g::kDrive);
  repairs = cst.diagnostics().size();
  return stats;
}

cursiv::LrParseStats parse_nl(const std::string& text, bool& accepted) {
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  accepted =
      cursiv::lr_parser<g::kTable>.parse(text, cst, &stats, &g::kDrive) &&
      cst.diagnostics().empty();
  return stats;
}

std::string tail(unsigned n) {
  std::string text = "p = = n";
  for (unsigned i = 0; i < n; ++i) text += " p(n)";
  return text;
}

}  // namespace

void run() {
  bool accepted = false;
  std::size_t repairs = 0;
  // The tail alone: one fork per statement, merged at the next.
  (void)parse(tail(64).substr(8), accepted, repairs);
  CHECK(accepted && repairs == 0);
  // The same tail behind one repair.
  (void)parse(tail(64), accepted, repairs);
  CHECK(accepted && repairs == 1);
  // Arms that shifted a line break apart merge like any other.
  std::string lines;
  for (unsigned i = 0; i < 64; ++i) lines += "k p a\nb;\n";
  (void)parse_nl(lines, accepted);
  CHECK(accepted);
}

}  // namespace ambiguous_tail

namespace case_lrrepairtables {
// Repair metadata must describe the emitted automata exactly, independently
// of the construction's successor rows and its fixed-point implementation.

namespace {

template <const auto& A>
void matches_the_emitted_automata() {
  using Set = std::remove_cvref_t<decltype(A.cold.follow[0])>;
  const auto n = A.dfaAccept.size();
  std::vector<std::vector<std::uint16_t>> successors(n);
  std::vector<std::uint8_t> bodies(n, 0);
  for (std::size_t s = 0; s < n; ++s) {
    auto& row = successors[s];
    for (unsigned b = 0; b < 256; ++b) {
      const auto to = A.next_byte(static_cast<std::uint16_t>(s),
                                  static_cast<unsigned char>(b));
      if (to == cursiv::kDeadState) continue;
      CHECK(to < n);
      if (to >= n) continue;
      if (std::find(row.begin(), row.end(), to) == row.end())
        row.push_back(to);
      bodies[s] |= A.dfaAccept[s] == 0 && to == s;
    }
  }
  const auto& actualBodies = A.derived.lexBodies;
  const auto& actualReach = A.derived.typoReach;
  CHECK(actualBodies.size() == n);
  for (std::size_t start = 0; start < n; ++start) {
    CHECK(actualBodies[start] == bodies[start]);
    Set reachable;
    bool owesCloser = false;
    std::vector<std::uint8_t> seen(n, 0);
    std::vector<std::uint16_t> todo{static_cast<std::uint16_t>(start)};
    seen[start] = 1;
    while (!todo.empty()) {
      const auto s = todo.back();
      todo.pop_back();
      reachable |= A.dfa_acc_set(s);
      owesCloser |= bodies[s] != 0;
      if constexpr (A.relays.size() != 0)
        owesCloser |= A.dfaRelay[s] != cursiv::kNoRelay;
      for (const auto to : successors[s])
        if (seen[to] == 0) {
          seen[to] = 1;
          todo.push_back(to);
        }
    }
    CHECK(actualReach[static_cast<std::uint16_t>(start)] == reachable);
    // A mode's first byte reaches this state exactly when that byte could
    // start a body or a relay. This computes forward reachability per state,
    // whereas the generator propagates information over reverse edges.
    const auto& openers = A.derived.lexOpeners;
    CHECK(openers.size() == A.modeStart.size());
    for (std::size_t mode = 0; mode < A.modeStart.size(); ++mode)
      for (unsigned b = 0; b < 256; ++b)
        if (A.next_byte(A.modeStart[mode], static_cast<unsigned char>(b)) ==
            start)
          CHECK(openers[mode].has(static_cast<unsigned char>(b)) ==
                owesCloser);
  }
  const auto& openers = A.derived.lexOpeners;
  for (std::size_t mode = 0; mode < A.modeStart.size(); ++mode)
    for (unsigned b = 0; b < 256; ++b)
      if (A.next_byte(A.modeStart[mode], static_cast<unsigned char>(b)) ==
          cursiv::kDeadState)
        CHECK(!openers[mode].has(static_cast<unsigned char>(b)));

  std::vector<Set> after(A.cfg.nterms), wanted(A.modeStart.size());
  for (std::uint32_t s = 0; s < A.mode.size(); ++s) {
    wanted[A.mode[s]] |= A.cold.follow[s];
    for (unsigned term = 0; term < A.cfg.nterms; ++term) {
      std::uint32_t first;
      unsigned count;
      cursiv::lr_action_run<A>(s, static_cast<cursiv::SymbolId>(term), first,
                                count);
      for (unsigned k = 0; k < count; ++k) {
        const auto action = cursiv::lr_shadow_action<A>(first + k);
        if (action.kind == cursiv::LrActionKind::shift)
          after[term] |= A.cold.follow[action.arg];
        if constexpr (cursiv::kLrGuarded<A>)
          if (action.kind == cursiv::LrActionKind::guard) {
            const auto at = A.guardAt[action.arg];
            const unsigned conditions = A.guardOps[at];
            for (unsigned outcome = 0; outcome < (1u << conditions);
                 ++outcome) {
              const auto to = A.guardOps[at + 1u + conditions + outcome];
              if (to != 0xffffu) after[term] |= A.cold.follow[to];
            }
          }
      }
    }
  }
  for (unsigned term = 0; term < A.cfg.nterms; ++term)
    CHECK(A.derived.after[term] == after[term]);
  for (std::size_t mode = 0; mode < A.modeStart.size(); ++mode)
    CHECK(A.derived.modeWant[mode] == wanted[mode]);
}

}  // namespace

void run() {
  matches_the_emitted_automata<tpar::generated::Lr::kTable>();
  matches_the_emitted_automata<tasi::cases::generated::G::kTable>();
  matches_the_emitted_automata<tnestedlex::generated::G::kTable>();
}
}  // namespace case_lrrepairtables

namespace case_lrtypoutf8 {
// A misspelling is one letter off, and a letter is a code point: a repair never
// ends in the middle of a UTF-8 sequence, and so no diagnostic does.

namespace {

namespace g = dm::generated::DomainModel;

bool on_a_boundary(std::string_view text, std::size_t offset) {
  return offset >= text.size() ||
         (static_cast<unsigned char>(text[offset]) & 0xC0) != 0x80;
}

void a_repair_never_cuts_a_code_point() {
  static const auto parser = g::create_parser();
  for (const std::string_view text :
       {"entit\xc3\xa9 Foo {}", "entity\xc3\xa9 Foo {}", "ent\xe2\x82\xac" "ty Foo {}",
        "entity Foo\xf0\x9f\x98\x80 {}", "entit\xc3\xa9\xc3\xa9 Foo {}"}) {
    const cursiv::ParseResult r = parser->parse(text);
    CHECK(!r.diagnostics.empty());
    for (const auto& d : r.diagnostics) {
      CHECK(on_a_boundary(text, d.begin));
      CHECK(on_a_boundary(text, d.end));
    }
  }
  std::printf("  a repair never cuts a code point\n");
}

}  // namespace

void run() {
  a_repair_never_cuts_a_code_point();
}
}  // namespace case_lrtypoutf8

int main() {
  repair_choice::run();
  repair_scale::run();
  repair_cost::run();
  ledger::run();
  ambiguous_tail::run();
  case_lrrepairtables::run();
  case_lrtypoutf8::run();
  a_stop_at_recovery_discards_the_partial_tree();
  phases_and_generators_name_what_they_stand_for();
  insertion_allowance_is_part_of_the_future();
  unread_bytes_take_one_insertion();
  merged_path_keeps_its_source_rank();
  for (const std::string_view t :
       {"entity E { a T }", "entity E { a: }", "entity E { a: T. }",
        "entity E {", "entity E { a: T b U }", "package p { entity E { a: T }",
        "entity E {{", "}", "entity E { a: T }}", "entity E { 1: T }",
        "datatype", "entity { }", "package p { entity E { a: T.U V b: W }",
        "package p { entity A { a: T b U } entity C { d: E } }",
        "package p { package q { entity B { b: T c: } } entity D {} }",
        "package p { entity A { a: B { b: C } entity D { d: E } }",
        "package p { entity A { a: B entity C { c: D } }",
        "entity E extends { a: T }",
        // A comment left open after debris: deleting the debris and reading
        // the comment as trivia is the cheapest way through.
        "entity E { a: T } } /* c\nentity F { }",
        "entity E { a: T } } } /* c\nentity F { }",
        "package p { entity E { } } } /* n\n entity G { } }"})
    exact<gdm::kTable>(gdm::kDrive, t);
  for (const std::string_view t :
       {"module m 1 + ;", "module m def f(a): a * ;", "module m (1 + 2;",
        "module m def : 1;", "module m 1 2;", "module m def f(a, : a;",
        "module m def f(a) a * 2; f(1) + 2; 3 * 4;",
        "module m 1 + * 2; def g(x): x; g(3);",
        "module m def h(a, b): (a + b; h(1, 2) * 3;", "module m f(1 2);",
        "module m f(1,,2);", "module m f(g(1);", "module m f(1,2",
        "module m f(", "module m f(1 2 3);", "module m f(1));",
        "module m f(1,2,);", "module m f(();", "module m f(1:2);",
        "module m f(,1);", "module m f(1, g(2 3), h(3, 4));",
        "module m f(g(1, 2), g(3 4), h(5));", "module m f(g(h(1, 2), 3);"})
    exact<ga::kTable>(ga::kDrive, t);

  unsigned read = 0;
  for (const char* doc :
       {"package p {\n  entity A { a: B many c: D.E }\n  datatype T\n"
        "  entity F extends A {}\n}\n",
        "// a comment\npackage q.r { package s { /* a note */ entity X {} } "
        "}\n"})
    read +=
        writes_as_drive<gdm::kTable>(gdm::kDrive, doc, "domainmodel") ? 1 : 0;
  for (const char* doc :
       {"module demo\n// a comment\ndef pi: 3.14159;\n"
        "def area(r): pi * r ^ 2;\narea(2 + 3) * 2;\n1 + 2 * 3 - 4 / 2;\n"})
    read += writes_as_drive<ga::kTable>(ga::kDrive, doc, "arith") ? 1
                                                                        : 0;
  CHECK(read == 3);

  // Whole valid documents written by the interpreted path, through the
  // graphs their cells open.
  {
    std::string nest;
    for (unsigned k = 0; k < 16; ++k)
      nest += "if c" + std::to_string(k) + " then ";
    nest += "x";
    for (unsigned k = 0; k < 16; ++k) nest += " else e" + std::to_string(k);
    CHECK(writes_through<gd::kTable>(gd::kDrive, nest, "dangling") != 0);
  }
  // The end of the input under a level whose indent a repair dropped, with no
  // last newline: the drive's dedents close the levels under the dropped
  // ones, and a dropped level alone owes none.
  {
    namespace gb = ::tindent::body::generated::G;
    for (const std::string_view text :
         {"a:\n  b\n    c\n    d\n    e\n    f",
          "a:\n  b:\n    c\n      d\n      e\n      f\n      g",
          "a\n  b\n  c\n  d\n  e"})
      repairs_through<gb::kTable>(gb::kDrive, text);
  }
  // A line break both trivia and token: the interpreted writer decides the
  // position of two candidates as the drive does, through `pick`.
  {
    (void)writes_through<gt::kTable>(gt::kDrive, "k pa\n\nb;\nk pa\nb;\n",
                                     "ambtail lines");
    CHECK(Interpreted<gt::kTable>::picks != 0);
  }

  // A separator missing between two short values is inserted: the author's
  // value stays.
  {
    const auto d = repaired<ga::kTable>("module m f(1 2);", ga::kDrive);
    CHECK(d.size() == 1 && d[0].kind == cursiv::DiagnosticKind::inserted);
  }
  // Parentheses owed at the end, however many: one fault, closed.
  {
    const auto d = repaired<ga::kTable>("module m ((((((((((((1", ga::kDrive);
    CHECK(d.size() == 1 && d[0].kind == cursiv::DiagnosticKind::inserted);
  }
  // A run of stray openers is one fault; past what the budget pays for
  // searching, still a few, and the bound says so.
  {
    const auto d =
        repaired<gdm::kTable>("entity A " + std::string(20, '{'), gdm::kDrive);
    CHECK(d.size() <= 2);
  }
  {
    const auto d =
        repaired<gdm::kTable>("entity A " + std::string(600, '{'), gdm::kDrive);
    CHECK(d.size() <= 4 && !d.empty() &&
          d.back().kind == cursiv::DiagnosticKind::bounded);
  }
  // A search stopped by its own share says so over its fault, which the
  // searches after it finish as one, and the next fault is searched for;
  // only the parse's budget running out leaves the rest to the first gesture
  // that holds.
  {
    constexpr std::string_view kDoc = "entity A {{{{{{{{{{{{ a: B c D }";
    const auto own = repaired<gdm::kTable>(
        kDoc, gdm::kDrive,
        cursiv::RepairBudget{
            .floor = 1u << 16, .perByte = 64, .perSearch = 1024});
    CHECK(own.size() == 3 && own[0].kind == cursiv::DiagnosticKind::deleted &&
          own[1].kind == cursiv::DiagnosticKind::bounded &&
          own[1].begin == own[0].begin && own[1].end == own[0].end &&
          own[2].kind == cursiv::DiagnosticKind::inserted &&
          own[2].begin == 28);
    const auto all = repaired<gdm::kTable>(
        kDoc, gdm::kDrive,
        cursiv::RepairBudget{
            .floor = 512, .perByte = 0, .perSearch = ~std::uint64_t{0}});
    CHECK(!says(all, cursiv::DiagnosticKind::inserted));
    CHECK(!all.empty() && all.back().kind == cursiv::DiagnosticKind::bounded &&
          all.back().end == kDoc.size());
  }
  // A parse's budget grows with the document up to one ceiling.
  {
    const std::vector<cursiv::IndentLevel> levels;
    const cursiv::LrSearch small(1000, cursiv::RepairBudget{}, levels);
    CHECK(small.budget ==
          cursiv::kRepairFloor + 1000 * cursiv::kRepairPerByte);
    const cursiv::LrSearch large(cursiv::TextOffset{1} << 20,
                                 cursiv::RepairBudget{}, levels);
    CHECK(large.budget == cursiv::kRepairPerParse);
  }
  // An indent dropped under a line that opened no suite: the line back at
  // the enclosing level closes nothing, whatever the state would take.
  {
    const auto d = repaired_indent("a:\n  b\n    c\n    d\n  e\nf\n");
    CHECK(d.size() == 1 && d[0].kind == cursiv::DiagnosticKind::deleted);
  }
  // A token invented at a line start leaves the line's run to the token the
  // author wrote: the drive reads it as the search did.
  {
    const auto d = repaired_indent("a:\n  b:\n  *  c\nx\n");
    CHECK(d.size() == 1);
  }
  return cursiv::test::report("LrRepairSearch_test");
}
