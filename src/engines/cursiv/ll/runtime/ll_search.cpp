// The steps of the repair search that read no table, compiled once for every
// language.
#include "cursiv/ll/runtime/ll_search.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/LlEdit.hpp"
#include "cursiv/ll/runtime/LlReading.hpp"
#include "cursiv/ll/runtime/LlSearch.hpp"
#include "cursiv/ll/table/LlSimulation.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/runtime/RepairSearch.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"

namespace cursiv::engine {

LlSearch::LlSearch(const LlAtnView& atn, TextOffset len, const RepairBudget& b)
    : RepairSearch(len, b), sim(atn) {}

LlSearch::~LlSearch() = default;

void ll_search_trim(LlSearch& s, const LlSearchCheckpoint& m) {
  while (s.nodes.size() > m.nodes && !s.nodes.back().queued) s.nodes.pop_back();
  if (s.facts.reading >= s.nodes.size()) s.facts.cfg = kLlNoNode;
  if (s.nodes.size() != m.nodes) return;
  s.pool.resize(m.pool);
  if (s.handed == m.handed) s.links.resize(m.links);
}

std::uint32_t ll_search_calls(LlSearch& s, std::uint32_t ctx) {
  std::uint32_t depth = 0;
  std::vector<std::uint32_t>& chain = s.chain;
  chain.clear();
  for (; ctx != kLlCtxNone; ctx = s.sim.contexts().parent(ctx, 0)) {
    if (const auto it = s.depths.find(ctx); it != s.depths.end()) {
      depth = it->second;
      break;
    }
    chain.push_back(ctx);
  }
  for (std::size_t k = chain.size(); k-- > 0;) s.depths[chain[k]] = ++depth;
  return depth;
}

std::uint32_t ll_search_source_tokens(const LlContext& c, LlSearch& s,
                                      std::uint32_t frame, std::uint32_t most) {
  const detail::CstRun& run = c.tape.stream();
  const std::size_t base = c.stack.size() - s.sourceTokens.size();
  for (std::size_t i = c.stack.size(); i-- > frame;) {
    if (s.sourceTokens[i - base] != kLlNoNode) continue;
    while (s.sourceScanned > c.stack.begin(i)) {
      if (ll_search_spent(s)) return 0;
      if (s.sourceCount > most) return s.sourceCount;
      const CstNode& n = run.at(--s.sourceScanned);
      s.sourceCount +=
          !n.hidden && !n.recovered && n.subtreeSize == 1 && n.end > n.begin
              ? 1u
              : 0u;
      ++s.work;
    }
    s.sourceTokens[i - base] = s.sourceCount;
  }
  return s.sourceTokens[frame - base];
}

std::uint32_t ll_search_child(LlSearch& s, std::uint32_t from,
                              LlEdit::Kind kind, SymbolId term, TextOffset end,
                              std::uint32_t choice, TextOffset begin) {
  s.nodes.push_back(s.nodes[from]);
  LlReading& e = s.nodes.back();
  e.link = s.link(LlEdit{.parent = e.link,
                         .choice = choice,
                         .kind = kind,
                         .term = term,
                         .end = end,
                         .at = e.gapFrom,
                         .begin = begin});
  e.faults += e.fresh ? 1u : 0u;
  e.edits += 1;
  e.fresh = false;
  // An insertion or an abandon leaves the token in hand where it was.
  e.inHand = e.inHand && (kind == LlEdit::ins || kind == LlEdit::abandon);
  e.shifts = 0;
  e.gone = e.queued = false;
  ++s.work;
  return static_cast<std::uint32_t>(s.nodes.size()) - 1;
}

std::uint64_t ll_edits_fold(const std::vector<LlEdit>& links, std::uint32_t l) {
  std::uint64_t h = 0;
  for (; links[l].parent != kNoEditLink; l = links[l].parent) {
    const LlEdit& g = links[l];
    h = (h ^ (std::uint64_t{g.kind} << 56) ^ (std::uint64_t{g.term} << 40) ^
         (std::uint64_t{g.end} << 20) ^ g.state ^ g.at ^
         (std::uint64_t{g.begin} << 8)) *
        0x100000001b3ull;
  }
  return h;
}

bool ll_same_edits(const std::vector<LlEdit>& links, std::uint32_t a,
                   std::uint32_t b) {
  for (; a != b; a = links[a].parent, b = links[b].parent) {
    if (links[a].parent == kNoEditLink || links[b].parent == kNoEditLink)
      return links[a].parent == links[b].parent;
    const LlEdit &x = links[a], &y = links[b];
    if (x.kind != y.kind || x.term != y.term || x.end != y.end ||
        x.state != y.state || x.at != y.at || x.begin != y.begin)
      return false;
  }
  return true;
}

void ll_search_reset(LlSearch& s) {
  s.restart();
  ll_clear(s.pool, s.nodes, s.seen, s.lexed, s.scans, s.readings, s.ends,
           s.typos, s.drops, s.plan, s.depths, s.sourceTokens);
  s.facts.cfg = kLlNoNode;
  s.sourceScanned = s.sourceCount = 0;
  s.next = 0;
  s.planned = false;
}

void ll_search_plan(LlSearch& s, std::uint32_t ci) {
  s.plan.clear();
  s.next = 0;
  for (std::uint32_t l = s.nodes[ci].link;
       l != kNoEditLink && s.links[l].parent != kNoEditLink;
       l = s.links[l].parent)
    s.plan.push_back(s.links[l]);
  std::ranges::reverse(s.plan);
  if (!s.plan.empty() && s.plan[0].kind == LlEdit::ins)
    std::erase(s.also, static_cast<ElementId>(s.plan[0].term));
  else
    s.also.clear();
}

std::uint32_t ll_search_open(const LlContext& c, LlSimulation& sim,
                             std::size_t frames) {
  sim.resume();
  sim.share_epsilon_contexts();
  sim.at_token(0);
  sim.keep_steps();
  sim.truncated(frames < c.stack.size());
  std::uint32_t ctx = kLlCtxNone;
  for (std::size_t i = c.stack.size() > frames ? c.stack.size() - frames : 0;
       i < c.stack.size(); ++i)
    ctx =
        sim.contexts().push(ctx, c.stack[i], -1 - static_cast<std::int64_t>(i));
  return ctx;
}

}  // namespace cursiv::engine
