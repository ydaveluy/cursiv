// What the episodes of a parse keep between them: the pool that holds the
// descent's stacks, the pool the episodes read in, the plan, and the plans
// remembered by the text they read; and the walk that lends an episode the
// descent's stack.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlEpisodeMemo.hpp"
#include "cursiv/engines/ll/runtime/LlEpisodeSim.hpp"
#include "cursiv/engines/ll/runtime/LlPlan.hpp"
#include "cursiv/engines/ll/runtime/LlPredictionWork.hpp"
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

// The sim of the parse's episodes, over the pool they read in.
template <const auto& T>
inline LlEpisodeSim& ll_episode_sim(bool tracked) {
  auto& cache = ll_episode_cache<T>();
  auto& slot = tracked ? cache.tracked : cache.plain;
  if (!slot) {
    slot.emplace(T.atn, ll_lent_pool<T>(), tracked);
    slot->rank_arms_over_lexes(T.repeatScopes);
    slot->share_calls(T.sharedRules);
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
