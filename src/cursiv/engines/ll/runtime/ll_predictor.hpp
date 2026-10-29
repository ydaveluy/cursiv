// Choices one token cannot settle use a generated automaton or an episode over
// the descent's real caller context.
#pragma once

#include <cstdint>

#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlEpisode.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"

namespace cursiv {

// The tokens the generated automaton of `T` read past the one in hand on this
// thread.
template <const auto& T>
inline std::uint64_t& ll_staged_reads() {
  thread_local std::uint64_t reads = 0;
  return reads;
}

// The readings of a choice, stepped over the text until the decisions they
// pass are the plan the descent is answered from. A choice made with readings
// left keeps them, to be read on for the next choice the descent asks of them.
template <const auto& T>
inline std::uint16_t ll_episode(LlContext& c, std::uint16_t decision) {
  LlEpisode<T>& episode = ll_episode_engine<T>();
  std::uint16_t arm = kLlUnplanned;
  if (!episode.resume(c, decision, arm)) arm = episode.run(c, decision);
  auto& cache = ll_episode_cache<T>();
  cache.work.tokens += episode.take_steps();
  return arm;
}

// The one door the generated code knocks on.
template <const auto& T>
inline std::uint16_t ll_predict(LlContext& c, std::uint16_t decision) {
  if (c.give) return kNoRow;
  const LlTable::Decision& dec = T.decisions[decision];
  if (const std::uint16_t planned = ll_planned<T>(c, decision);
      planned != kLlUnplanned)
    return planned;
  if (dec.how == LlPredict::staged) {
    const std::uint16_t a = T.staged(c, decision);
    if (a != kNoRow) {
      ll_plan_went<T>(c, decision, a);
      return a;
    }
  }
  return ll_episode<T>(c, decision);
}

}  // namespace cursiv
