// What the episodes of a parse keep between them: the stack pools, the plan,
// the plans keyed by the text they read, and the walk that gives an episode the
// descent's stack.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/LlEpisodeMemo.hpp"
#include "cursiv/ll/runtime/LlEpisodeSim.hpp"
#include "cursiv/ll/runtime/LlPlan.hpp"
#include "cursiv/ll/runtime/LlPredictionWork.hpp"
#include "cursiv/ll/table/LlSimulation.hpp"
#include "cursiv/ll/table/LlTable.hpp"

namespace cursiv::engine {

// What the episodes of one parse keep: the pool that holds the return stacks,
// a sim each for the two ways of reading, the plan the last episode made for
// the descent to follow, and what they read.
struct LlEpisodeCache {
  // The descent's stacks, as nodes that stay: the plans kept are named by them.
  std::optional<LlReturnPool> exact;
  // The stacks an episode makes, which go when it ends or is let go.
  std::optional<LlReturnPool> temporary;
  // Where each node of `exact` stands in `temporary`.
  std::vector<std::uint32_t> copyOf;
  // How deep each copied frame stands, by its node in `temporary`.
  std::vector<LlCopyDepth> copyDepth;
  std::optional<LlEpisodeSim> plain, tracked;
  LlPlan plan;
  LlPredictionWork work;
  // Where the entry rule returns to, once a stack has asked.
  std::uint32_t document = kLlCtxNone;
  // The plans kept per choice, by the text each read.
  std::vector<LlEpisodeMemo> memo;
};

// A public parse owns its learned state: a plan names places in one document.
template <const auto& T>
inline std::optional<LlEpisodeCache>*& ll_episode_session_slot() {
  thread_local std::optional<LlEpisodeCache>* active = nullptr;
  return active;
}

template <const auto& T>
inline LlEpisodeCache& ll_episode_cache() {
  auto& active = *ll_episode_session_slot<T>();
  return active.has_value() ? *active : active.emplace();
}

// A pool of the parse's cache, made at its first ask: `exact`, or `temporary`.
template <const auto& T>
inline LlReturnPool& ll_pool(bool exact) {
  LlEpisodeCache& cache = ll_episode_cache<T>();
  auto& pool = exact ? cache.exact : cache.temporary;
  if (!pool) {
    pool.emplace();
    pool->read_returns_by_class(T.atn.returnClass);
  }
  return *pool;
}

// The sim of the parse's episodes, over the pool they read in.
template <const auto& T>
inline LlEpisodeSim& ll_episode_sim(bool tracked) {
  auto& cache = ll_episode_cache<T>();
  auto& slot = tracked ? cache.tracked : cache.plain;
  if (!slot) {
    slot.emplace(T.atn, ll_pool<T>(false), tracked);
    slot->rank_arms_over_lexes(T.repeatScopes);
    slot->share_calls(T.sharedRules);
    slot->read_copy_depths(&cache.copyDepth);
  }
  slot->reset();
  return *slot;
}

// Where the entry rule returns to: the document, whatever other calls of it the
// grammar has.
template <const auto& T>
inline std::uint32_t ll_document_context(LlReturnPool& pool) {
  auto& cache = ll_episode_cache<T>();
  if (cache.document == kLlCtxNone) {
    const LlAtnState& entry = T.atn.state(T.atnStart);
    cache.document = pool.push_return(
        kLlCtxNone, pool.return_of(T.atn.edge(entry.edge).follow));
  }
  return cache.document;
}

// The descent's stack, copied into the episodes' pool. Each copied frame
// keeps its depth, so a walk that reaches one over the episode's frames answers
// without walking up the descent's depth.
inline std::uint32_t ll_copy_stack(LlEpisodeCache& cache, std::uint32_t stack) {
  LlCtxPool& from = cache.exact->contexts();
  LlCtxPool& to = cache.temporary->contexts();
  // `copyOf` and `copyDepth` are never cleared: they hold only because the
  // copied frames are made outside a walk, which would take them back with its
  // own.
  assert(!to.scratching());
  std::vector<std::uint32_t> chain;
  std::uint32_t at = stack, base = kLlCtxNone;
  for (; at != kLlCtxNone; at = from.parent(at, 0)) {
    if (at < cache.copyOf.size() && cache.copyOf[at] != kLlCtxNone) {
      base = cache.copyOf[at];
      break;
    }
    chain.push_back(at);
  }
  for (std::size_t i = chain.size(); i-- > 0;) {
    const std::uint32_t parent = base;
    base = to.push(parent, from.ret(chain[i], 0));
    if (cache.copyOf.size() <= chain[i])
      cache.copyOf.resize(chain[i] + 1, kLlCtxNone);
    cache.copyOf[chain[i]] = base;
    if (cache.copyDepth.size() <= base) cache.copyDepth.resize(base + 1);
    if (cache.copyDepth[base].depth == 0)
      cache.copyDepth[base] = LlCopyDepth::over(cache.copyDepth, parent);
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
        _pool(ll_pool<T>(false)),
        key(c.stack.context(ll_pool<T>(true),
                            ll_document_context<T>(ll_pool<T>(true)))),
        context(ll_copy_stack(*_cache, key)),
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

}  // namespace cursiv::engine
