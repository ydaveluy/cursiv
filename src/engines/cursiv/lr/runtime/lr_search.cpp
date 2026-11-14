// The steps of the repair search that read no table, compiled once for every
// language.
#include "cursiv/lr/runtime/lr_search.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/lr/runtime/LrCactus.hpp"
#include "cursiv/lr/runtime/LrConfig.hpp"
#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrDrive.hpp"
#include "cursiv/lr/runtime/LrEdit.hpp"
#include "cursiv/lr/runtime/LrExit.hpp"
#include "cursiv/lr/runtime/LrFrame.hpp"
#include "cursiv/lr/runtime/LrSearch.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/runtime/RepairFault.hpp"
#include "cursiv/shared/runtime/RepairSearch.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"

namespace cursiv::engine {

LrSearch::LrSearch(TextOffset len, const RepairBudget& b,
                   const std::vector<IndentLevel>& levels)
    : RepairSearch(len, b), seen(0, FutureHash{}, FutureSame{&levels}) {}

LrSearch::~LrSearch() = default;

void lr_search_trim(LrSearch& s, const LrSearchCheckpoint& m) {
  while (s.configs.size() > m.configs && !s.configs.back().queued)
    s.configs.pop_back();
  if (s.configs.size() != m.configs || s.handed != m.handed) return;
  s.links.resize(m.links);
  s.cactus.rewind(m.cactus);
}

void lr_search_admit(LrSearch& s, std::uint32_t i) {
  const LrConfig& r = s.configs[i];
  assert(!r.generates());
  const std::uint32_t h =
      !lr_search_goal(r) && r.freeInserts == 0 ? kRepairReread : 0;
  const auto [it, fresh] = s.seen.try_emplace(LrSearch::Future::of(r), i);
  if (!fresh && repair_merge<&LrConfig::freeInserts>(
                    s, s.configs, i, it->second, lr_search_kind))
    return;
  lr_search_queue(s, r.cost + h, r, i);
}

void lr_search_count_work(const LrContext& c, LrSearch& s) {
  s.work += s.cactus.stepped_units(kRepairLexBytes);
  const std::uint64_t read = c.gaps.read() - s.gapsCounted;
  if (read < kRepairLexBytes) return;
  s.work += read / kRepairLexBytes;
  s.gapsCounted += read - read % kRepairLexBytes;
}

std::int64_t lr_search_src_tokens(const LrContext& c, LrSearch& s,
                                  std::uint32_t f, std::int64_t most) {
  if (s.srcTokens.size() != s.cactus.frozen_count())
    s.srcTokens.assign(s.cactus.frozen_count(), -1);
  if (s.srcTokens[f] >= 0) return s.srcTokens[f];
  const LrCactus::Frozen fz = s.cactus.frozen(f);
  LrSearch::Source& so = s.sources[fz.source];
  const std::uint32_t first = f - fz.frame;
  for (std::uint32_t j = so.frames->size(); j-- > fz.frame;) {
    if (s.srcTokens[first + j] >= 0) continue;
    const NodeCount open = (*so.frames)[j].open;
    for (; so.scanned > open; --so.scanned) {
      if (so.count > most) return so.count;
      ++s.work;
      if (so.scanned > c.tape.len()) {
        ++so.count;
        continue;
      }
      const CstNode& n = c.tape.at(so.scanned - 1);
      if (n.subtreeSize == 1 && !n.hidden && !n.recovered && n.begin < n.end)
        ++so.count;
    }
    s.srcTokens[first + j] = so.count;
  }
  return s.srcTokens[f];
}

std::uint32_t lr_search_child(LrSearch& s, std::uint32_t from,
                              LrEdit::Kind kind, SymbolId term,
                              std::uint32_t end, std::uint32_t choice) {
  s.configs.push_back(s.configs[from]);
  LrConfig& e = s.configs.back();
  e.link = s.link(LrEdit{.parent = e.link,
                         .choice = choice,
                         .kind = kind,
                         .opens = e.fresh,
                         .term = term,
                         .end = end,
                         .at = e.at,
                         .la = e.la,
                         .node = e.run.top(),
                         .low = e.run.low(),
                         .cost = e.cost});
  e.faults += e.fresh ? 1u : 0u;
  e.edits += 1;
  e.fresh = false;
  e.shifts = 0;
  e.dead = e.gone = e.queued = e.pinDue = false;
  ++s.work;
  return static_cast<std::uint32_t>(s.configs.size()) - 1;
}

bool lr_same_edits(const std::vector<LrEdit>& links, std::uint32_t a,
                   std::uint32_t b) {
  const auto drives = [&](std::uint32_t l) {
    return l != kNoEditLink && links[l].kind == LrEdit::arm && !links[l].held &&
           links[l].parent != kNoEditLink;
  };
  for (;;) {
    while (drives(a)) a = links[a].parent;
    while (drives(b)) b = links[b].parent;
    if (a == b) return true;
    if (a == kNoEditLink || b == kNoEditLink) return false;
    const LrEdit& x = links[a];
    const LrEdit& y = links[b];
    if (x.parent == kNoEditLink || y.parent == kNoEditLink ||
        x.kind != y.kind || x.term != y.term || x.end != y.end ||
        x.at != y.at || !(x.la == y.la) ||
        (x.kind == LrEdit::arm && x.choice != y.choice))
      return false;
    a = x.parent;
    b = y.parent;
  }
}

std::uint64_t lr_edits_fold(const std::vector<LrEdit>& links, std::uint32_t l) {
  std::uint64_t h = 0;
  for (; links[l].parent != kNoEditLink; l = links[l].parent) {
    const LrEdit& g = links[l];
    if (g.kind == LrEdit::arm && !g.held) continue;
    const std::uint64_t v =
        (std::uint64_t{g.kind} << 56) ^ (std::uint64_t{g.term} << 40) ^
        (std::uint64_t{g.la.sym} << 24) ^ (std::uint64_t{g.end} << 20) ^ g.at ^
        (g.kind == LrEdit::arm ? std::uint64_t{g.choice} << 48 : 0);
    h = (h ^ v) * 0x100000001b3ull;
  }
  return (h ^ l) * 0x100000001b3ull;
}

void lr_search_reset(LrSearch& s) {
  const auto clear = [](auto&... x) { (x.clear(), ...); };
  s.restart();
  s.cactus.reset();
  clear(s.sources, s.configs, s.seen, s.srcTokens, s.lexed, s.readings, s.ends,
        s.typos, s.written, s.plan, s.planArms);
  s.errorEnds.reset();
  s.next = 0;
}

void lr_search_start(const LrContext& c, LrSearch& s) {
  lr_search_reset(s);
  s.costBase = c.cost;
  // A fault a stopped search left open goes on.
  lr_search_root(s, c, c.committed, c.stack, c.tape.len(), 0, !s.fault.open);
}

void lr_search_plan(const LrContext& c, LrSearch& s, std::uint32_t ci,
                    bool open) {
  std::vector<std::uint32_t> path;
  for (std::uint32_t l = s.configs[ci].link;
       l != kNoEditLink && s.links[l].parent != kNoEditLink;
       l = s.links[l].parent) {
    path.push_back(l);
  }
  s.plan.clear();
  s.planArms.clear();
  s.next = 0;
  for (std::size_t k = path.size(); k-- > 0;) {
    const LrEdit& g = s.links[path[k]];
    if (g.kind != LrEdit::arm) {
      LrSearch::Step st;
      st.g = g;
      st.armBase = static_cast<std::uint32_t>(s.planArms.size());
      s.plan.push_back(st);
    } else if (g.held && !s.plan.empty()) {
      s.planArms.push_back(g.choice);
      ++s.plan.back().armCount;
    }
  }
  for (std::size_t k = 0; k < s.plan.size(); ++k) {
    LrSearch::Step& st = s.plan[k];
    st.closes = k + 1 == s.plan.size() || s.plan[k + 1].g.opens;
    st.stepCost =
        (k + 1 < s.plan.size() ? s.plan[k + 1].g.cost : s.configs[ci].cost) -
        st.g.cost;
    st.kept = c.stack[std::max(st.g.low, 1u) - 1];
  }
  if (open && !s.plan.empty() && !lr_search_goal(s.configs[ci]))
    s.plan.back().closes = false;
  if (!s.plan.empty() && s.plan[0].g.kind == LrEdit::ins)
    std::erase(s.also, static_cast<ElementId>(s.plan[0].g.term));
  else
    s.also.clear();
}

bool lr_search_holds(const LrContext& c, const LrSearch& s,
                     const LrSearch::Step& st) {
  if (c.laMode == kNoMode || !(c.la == st.g.la) ||
      c.cost != s.costBase + st.g.cost)
    return false;
  std::uint32_t n = st.g.node;
  const std::uint32_t depth = s.cactus.depth(n);
  if (c.stack.size() != depth) return false;
  const std::uint32_t low = std::max(st.g.low, 1u);
  for (std::uint32_t d = depth; d-- > low;) {
    if (c.stack[d].state != s.cactus.state(n)) return false;
    n = s.cactus.parent(n);
  }
  const LrFrame& kept = c.stack[low - 1];
  return kept.state == st.kept.state && kept.open == st.kept.open &&
         kept.begin == st.kept.begin;
}

void lr_search_note(LrContext& c, LrSearch& s) {
  RepairFault& f = s.fault;
  if (!f.open) return;
  RepairNote n = repair_note(f);
  assert((c.notes.empty() || c.notes.back().end <= n.end) &&
         "a reading never adds a note behind the last one");
  if (s.bounded && n.begin < s.boundedBegin) s.boundedBegin = n.begin;
  c.notes.push_back(std::move(n));
  f = RepairFault{};
}

void lr_search_bounded(LrContext& c, LrSearch& s) {
  if (!s.bounded || c.cost < s.boundedLeast) return;
  s.bounded = false;
  s.boundedLeast = ~std::uint64_t{0};
  RepairNote n{};
  n.kind = DiagnosticKind::Bounded;
  n.begin = s.boundedBegin;
  n.end = s.boundedEnd > s.boundedBegin ? s.boundedEnd : s.boundedBegin;
  if (!c.notes.empty() && c.notes.back().end > n.end)
    n.end = c.notes.back().end;
  c.notes.push_back(n);
}

LrExit lr_search_wrote(LrContext& c, LrSearch& s) {
  const bool end = s.next == s.plan.size();
  if (end && s.fault.open) s.openAt = c.committed;
  if (s.degraded) s.degradedShifts = c.shifts;
  if (s.bounded) {
    s.boundedEnd = c.committed;
    if (end && !s.fault.open && !s.degraded) lr_search_bounded(c, s);
  }
  return lr_resume(c);
}

void lr_search_finish(LrContext& c, LrSearch& s) {
  lr_search_note(c, s);
  lr_search_bounded(c, s);
  c.stats.work = s.work;
  c.stats.searchRoom = static_cast<std::uint32_t>(s.configs.capacity());
}

}  // namespace cursiv::engine
