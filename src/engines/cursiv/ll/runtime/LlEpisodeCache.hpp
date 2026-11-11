// What the episodes of a parse keep between them: the stack pool, the plan,
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
  // The descent's stacks, as nodes that stay and name the plans kept, under
  // the stacks an episode makes, which go when it ends or is let go.
  std::optional<LlReturnPool> pool;
  // How deep each frame of the descent stands, by its node.
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

// The pool of the parse's cache, made at its first ask.
template <const auto& T>
inline LlReturnPool& ll_pool() {
  LlEpisodeCache& cache = ll_episode_cache<T>();
  if (!cache.pool) {
    cache.pool.emplace();
    cache.pool->read_returns_by_class(T.atn.returnClass);
  }
  return *cache.pool;
}

// The sim of the parse's episodes, over the pool they read in.
template <const auto& T>
inline LlEpisodeSim& ll_episode_sim(bool tracked) {
  auto& cache = ll_episode_cache<T>();
  auto& slot = tracked ? cache.tracked : cache.plain;
  if (!slot) {
    slot.emplace(T.atn, ll_pool<T>(), tracked);
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

// The descent's stack in the parse's pool. Each of its frames keeps its depth,
// so a walk that reaches one over the episode's frames answers without walking
// up the descent's depth.
template <const auto& T>
inline std::uint32_t ll_descent_stack(const LlContext& c) {
  LlEpisodeCache& cache = ll_episode_cache<T>();
  LlReturnPool& pool = ll_pool<T>();
  // The frames are made outside a walk, which would take them back with its
  // own: `copyDepth` is never cleared.
  assert(!pool.contexts().scratching());
  const std::uint32_t stack =
      c.stack.context(pool, ll_document_context<T>(pool));
  const auto indexed = [&](std::uint32_t at) {
    return at < cache.copyDepth.size() && cache.copyDepth[at].depth != 0;
  };
  std::vector<std::uint32_t> chain;
  for (std::uint32_t at = stack; at != kLlCtxNone && !indexed(at);
       at = pool.contexts().parent(at, 0))
    chain.push_back(at);
  // A frame is made after the one under it: the top one is the last.
  if (!chain.empty() && cache.copyDepth.size() <= chain.front())
    cache.copyDepth.resize(chain.front() + 1);
  for (std::size_t i = chain.size(); i-- > 0;)
    cache.copyDepth[chain[i]] =
        LlCopyDepth::over(cache.copyDepth, pool.contexts().parent(chain[i], 0));
  return stack;
}

// The stack of the descent for one episode: a node of the pool that stays,
// which names the episode's question; the nodes the episode makes over it go
// when it ends.
template <const auto& T>
struct LlExactWalk {
  explicit LlExactWalk(const LlContext& c)
      : context(ll_descent_stack<T>(c)),
        _cache(&ll_episode_cache<T>()),
        _pool(ll_pool<T>()),
        _holds(_pool.contexts().begin_scratch()) {}
  ~LlExactWalk() {
    if (!_holds) return;
    _pool.contexts().end_scratch();
    if (_cache->plain) _cache->plain->trim();
    if (_cache->tracked) _cache->tracked->trim();
  }
  LlExactWalk(const LlExactWalk&) = delete;
  LlExactWalk& operator=(const LlExactWalk&) = delete;

  const std::uint32_t context;

 private:
  LlEpisodeCache* _cache;
  LlReturnPool& _pool;
  bool _holds;
};

}  // namespace cursiv::engine
