// What the episodes of a parse keep between them: the pool that holds the
// descent's stacks, the pool the episodes read in, the plan, and the plans
// remembered by the text they read; and the walk that lends an episode the
// descent's stack.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlEpisodeMemo.hpp"
#include "cursiv/engines/ll/runtime/LlEpisodeSim.hpp"
#include "cursiv/engines/ll/runtime/LlPlan.hpp"
#include "cursiv/engines/ll/runtime/LlPredictionWork.hpp"
#include "cursiv/engines/ll/runtime/LlThread.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"

namespace cursiv {

// What the episodes of one parse keep: the pool that holds the return stacks,
// a sim each for the two ways of reading, the plan the last episode made for
// the descent to follow, and what they read.
struct LlEpisodeCache {
  // The descent's stacks, as nodes that stay: the plans kept are named by them.
  std::optional<LlReturnPool> exact;
  // The stacks an episode makes, which go when it ends or is let go.
  std::optional<LlReturnPool> lent;
  // Where each node of `exact` stands in `lent`.
  std::vector<std::uint32_t> lentOf;
  std::optional<LlEpisodeSim> plain, tracked;
  LlPlan plan;
  LlPredictionWork work;
  // Where the entry rule returns to, in the pool of that life.
  std::uint64_t documentLife = 0;
  std::uint32_t document = 0;
  // The plans kept per choice, by the text each read.
  std::vector<LlEpisodeMemo> memo;
};

// A public parse owns its learned state. Raw prediction helpers outside a
// parse keep no plan between calls: a plan names places in one document.
template <const auto& T>
inline std::optional<LlEpisodeCache>*& ll_episode_session_slot() {
  thread_local std::optional<LlEpisodeCache>* active = nullptr;
  return active;
}

template <const auto& T>
inline LlEpisodeCache& ll_episode_cache() {
  if (auto* active = ll_episode_session_slot<T>()) {
    if (!active->has_value()) active->emplace();
    return active->value();
  }
  thread_local LlEpisodeCache cache;
  return cache;
}

template <const auto& T>
inline LlReturnPool& ll_exact_pool() {
  auto& exact = ll_episode_cache<T>().exact;
  if (!exact) {
    exact.emplace();
    exact->read_returns_by_class(T.atn.returnClass);
  }
  return *exact;
}

template <const auto& T>
inline LlReturnPool& ll_lent_pool() {
  auto& lent = ll_episode_cache<T>().lent;
  if (!lent) {
    lent.emplace();
    lent->read_returns_by_class(T.atn.returnClass);
  }
  return *lent;
}

// The rules that two ways out of one choice can call before a token is read:
// a closure reaches them over two stacks, and the body of each is read again
// unless the callers share it. A rule that can end unread is not shared: its
// callers join only after it has left.
template <const auto& T>
inline const std::uint8_t* ll_shared_rules() {
  static const std::vector<std::uint8_t> flags = [] {
    const std::size_t words = (std::size_t{T.nrules} + 63u) / 64u;
    std::vector<std::uint8_t> nullable(T.nrules, 0), shared(T.nrules, 0);
    std::vector<std::uint64_t> region(std::size_t{T.nrules} * words, 0);
    std::vector<std::uint32_t> seen(T.atn.nstates, 0), todo;
    std::uint32_t round = 0;
    const auto set = [&](std::uint64_t* bits, std::uint16_t rule) {
      bits[rule >> 6] |= std::uint64_t{1} << (rule & 63u);
    };
    // The rules called from `from` before a token is read, into `bits`:
    // whether the body of `rule` can end there.
    const auto scan = [&](std::uint32_t from, std::uint16_t rule,
                          std::uint64_t* bits) {
      bool ends = false;
      ++round;
      todo.assign(1, from);
      while (!todo.empty()) {
        const std::uint32_t at = todo.back();
        todo.pop_back();
        if (seen[at] == round) continue;
        seen[at] = round;
        const LlAtnState& state = T.atn.state(at);
        ends =
            ends || (state.kind == LlAtnKind::ruleStop && state.rule == rule);
        for (std::uint32_t i = 0; i < state.nedges; ++i) {
          const LlAtnEdge& e = T.atn.edge(state.edge + i);
          if (e.kind == LlEdgeKind::epsilon) {
            todo.push_back(e.target);
          } else if (e.kind == LlEdgeKind::rule) {
            set(bits, e.label);
            for (std::size_t w = 0; w < words; ++w)
              bits[w] |= region[std::size_t{e.label} * words + w];
            if (nullable[e.label] != 0) todo.push_back(e.follow);
          }
        }
      }
      return ends;
    };
    std::vector<std::uint64_t> bits(words);
    for (bool grew = true; grew;) {
      grew = false;
      for (std::uint16_t rule = 0; rule < T.nrules; ++rule) {
        std::ranges::fill(bits, 0);
        const bool ends = scan(T.atn.ruleStart[rule], rule, bits.data());
        if (ends && nullable[rule] == 0) {
          nullable[rule] = 1;
          grew = true;
        }
        for (std::size_t w = 0; w < words; ++w) {
          std::uint64_t& kept = region[std::size_t{rule} * words + w];
          if ((kept | bits[w]) != kept) {
            kept |= bits[w];
            grew = true;
          }
        }
      }
    }
    std::vector<std::uint64_t> once(words), twice(words);
    for (std::uint32_t at = 0; at < T.atn.nstates; ++at) {
      const LlAtnState& state = T.atn.state(at);
      if (state.nedges < 2) continue;
      std::ranges::fill(once, 0);
      std::ranges::fill(twice, 0);
      for (std::uint32_t i = 0; i < state.nedges; ++i) {
        const LlAtnEdge& e = T.atn.edge(state.edge + i);
        std::ranges::fill(bits, 0);
        if (e.kind == LlEdgeKind::epsilon) {
          scan(e.target, state.rule, bits.data());
        } else if (e.kind == LlEdgeKind::rule) {
          set(bits.data(), e.label);
          for (std::size_t w = 0; w < words; ++w)
            bits[w] |= region[std::size_t{e.label} * words + w];
          if (nullable[e.label] != 0) scan(e.follow, state.rule, bits.data());
        }
        for (std::size_t w = 0; w < words; ++w) {
          twice[w] |= once[w] & bits[w];
          once[w] |= bits[w];
        }
      }
      for (std::uint16_t rule = 0; rule < T.nrules; ++rule)
        if (((twice[rule >> 6] >> (rule & 63u)) & 1u) != 0 &&
            nullable[rule] == 0)
          shared[rule] = 1;
    }
    return shared;
  }();
  return flags.data();
}

// The repetitions that take another turn before they return to the caller
// when both read the text: a token of the caller's, longer, does not override
// a continuation of the body. Null where no repetition does.
template <const auto& T>
inline const std::uint8_t* ll_repeat_scopes() {
  static const std::vector<std::uint8_t> flags = [] {
    std::vector<std::uint8_t> out(T.ndecisions, 0);
    LlReturnPool pool;
    pool.read_returns_by_class(T.atn.returnClass);
    LlEpisodeSim sim(T.atn, pool, false);
    sim.share_calls(ll_shared_rules<T>());
    std::vector<LlThread> seeded;
    std::vector<std::pair<std::uint32_t, std::uint16_t>> visited;
    for (std::uint16_t d = 0; d < T.ndecisions; ++d) {
      const LlTable::Decision& dec = T.decisions[d];
      if (!dec.repeat) continue;
      sim.reset();
      visited.clear();
      sim.seed(dec.state, kLlCtxNone, seeded, &visited);
      bool local = false, caller = false;
      for (const auto& [at, root] : visited) {
        if (root != 1) continue;
        const auto& state = T.atn.state(at);
        caller = caller ||
                 (state.kind == LlAtnKind::ruleStop && state.rule == dec.rule);
        for (std::uint32_t i = 0; i < state.nedges; ++i)
          if (T.atn.edge(state.edge + i).kind == LlEdgeKind::atom) {
            local = local || state.rule == dec.rule;
            caller = caller || state.rule != dec.rule;
          }
      }
      out[d] = !local || caller;
    }
    return out;
  }();
  static const bool any =
      std::ranges::any_of(flags, [](std::uint8_t f) { return f != 0; });
  return any ? flags.data() : nullptr;
}

// The sim of the parse's episodes, over the pool they read in.
template <const auto& T>
inline LlEpisodeSim& ll_episode_sim(bool tracked) {
  auto& cache = ll_episode_cache<T>();
  auto& slot = tracked ? cache.tracked : cache.plain;
  if (!slot) {
    slot.emplace(T.atn, ll_lent_pool<T>(), tracked);
    slot->rank_arms_over_lexes(ll_repeat_scopes<T>());
    slot->share_calls(ll_shared_rules<T>());
  }
  slot->reset();
  return *slot;
}

// Where the entry rule returns to: the document, whatever other calls of it the
// grammar has. A rule that is not the entry returns to a caller not known.
template <const auto& T>
inline std::uint32_t ll_document_context(
    LlReturnPool& pool, bool document) {
  if (!document) return kLlCtxNone;
  auto& cache = ll_episode_cache<T>();
  if (cache.documentLife != pool.contexts().life()) {
    cache.documentLife = pool.contexts().life();
    const LlAtnState& entry = T.atn.state(T.atnStart);
    cache.document = pool.push_return(
        kLlCtxNone, pool.return_of(T.atn.edge(entry.edge).follow));
  }
  return cache.document;
}

// The stack of the descent, in the pool the episodes read in.
inline std::uint32_t ll_lend_stack(LlEpisodeCache& cache, std::uint32_t stack) {
  LlCtxPool& from = cache.exact->contexts();
  LlCtxPool& to = cache.lent->contexts();
  std::vector<std::uint32_t> chain;
  std::uint32_t at = stack, base = kLlCtxNone;
  for (; at != kLlCtxNone; at = from.parent(at, 0)) {
    if (at < cache.lentOf.size() && cache.lentOf[at] != kLlCtxNone) {
      base = cache.lentOf[at];
      break;
    }
    chain.push_back(at);
  }
  for (std::size_t i = chain.size(); i-- > 0;) {
    base = to.push(base, from.ret(chain[i], 0));
    if (cache.lentOf.size() <= chain[i])
      cache.lentOf.resize(chain[i] + 1, kLlCtxNone);
    cache.lentOf[chain[i]] = base;
  }
  return base;
}

// The stack of the descent for one episode: a node of the pool that stays,
// which names the episode's question, and the same stack in the pool the
// episode reads in, whose nodes go when it ends.
template <const auto& T>
struct LlExactWalk {
  explicit LlExactWalk(const LlContext& c)
      : _cache(&ll_episode_cache<T>()),
        _pool(ll_lent_pool<T>()),
        key(c.stack.context(ll_exact_pool<T>(),
                             ll_document_context<T>(ll_exact_pool<T>(), c.document))),
        context(ll_lend_stack(*_cache, key)),
        _holds(_pool.contexts().begin_scratch()) {}
  ~LlExactWalk() {
    if (!_holds) return;
    _pool.contexts().end_scratch();
    if (_cache->plain) _cache->plain->trim();
    if (_cache->tracked) _cache->tracked->trim();
  }
  LlExactWalk(const LlExactWalk&) = delete;
  LlExactWalk& operator=(const LlExactWalk&) = delete;

 private:
  LlEpisodeCache* _cache;
  LlReturnPool& _pool;

 public:
  std::uint32_t key, context;

 private:
  bool _holds;
};

}  // namespace cursiv
