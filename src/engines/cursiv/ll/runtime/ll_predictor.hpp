// Choices one token cannot decide use a generated automaton or an episode over
// the descent's real caller context.
#pragma once

#include <cstdint>

#include "cursiv/utils/macros.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/LlEpisode.hpp"
#include "cursiv/ll/table/LlTable.hpp"

namespace cursiv::engine {

// The tokens the generated automaton of `T` read past the one in hand on this
// thread.
template <const auto& T>
inline std::uint64_t& ll_staged_reads() {
  thread_local std::uint64_t reads = 0;
  return reads;
}

// A choice none of whose readings takes the token in hand: its terminal comes
// off and the position is read again while the arms read the token. Whether a
// reading took it, its arm in `arm`; otherwise the first token read stays.
template <const auto& T>
inline bool ll_read_refused(LlContext& c, std::uint16_t decision,
                            LlEpisode<T>& episode, std::uint16_t& arm) {
  const LlHeldRead first = ll_hold_read(c);
  LlSkipped<T> skip{};
  bool again = false;
  std::vector<std::uint16_t> ties;
  while (T.holds(T.dec_first(decision), c.la.sym) &&
         ll_read_without<T>(c, T.decisions[decision].mode, skip)) {
    ll_ties_without<T>(c, T.decisions[decision].mode, skip, ties);
    const std::uint16_t read = episode.run(c, decision, &ties);
    if (!episode.refused()) {
      arm = read;
      if (first.mode == kLaHeld) ll_hold<T>(c);
      return true;
    }
    again = true;
  }
  ll_give_back(c, first);
  if (again) episode.run(c, decision);
  return false;
}

// A choice a reading stopped at: it read the longest token, and a condition may
// have refused that one. The arm a reading without it takes, or none. The
// choice is read again only where another token reads there.
template <const auto& T>
CURSIV_NOINLINE inline std::uint16_t ll_read_stopped(LlContext& c,
                                                     std::uint16_t decision) {
  const LlHeldRead first = ll_hold_read(c);
  LlSkipped<T> skip{};
  const bool other = T.holds(T.dec_first(decision), c.la.sym) &&
                     ll_read_without<T>(c, T.decisions[decision].mode, skip);
  ll_give_back(c, first);
  std::uint16_t arm = kNoRow;
  if (other) {
    LlEpisode<T>& episode = ll_episode_engine<T>();
    episode.run(c, decision);
    if (episode.refused()) ll_read_refused<T>(c, decision, episode, arm);
    ll_episode_cache<T>().work.tokens += episode.take_steps();
  }
  return arm;
}

// The readings of a choice, stepped over the text until the decisions they
// pass are the plan the descent is answered from. A choice made with readings
// left keeps them, to be read on for the next choice the descent asks of them.
template <const auto& T>
CURSIV_NOINLINE inline std::uint16_t ll_episode(LlContext& c,
                                                std::uint16_t decision) {
  LlEpisode<T>& episode = ll_episode_engine<T>();
  std::uint16_t arm = kLlUnplanned;
  if (!episode.resume(c, decision, arm)) {
    arm = episode.run(c, decision);
    if constexpr (T.ngapConds != 0)
      if (episode.refused()) ll_read_refused<T>(c, decision, episode, arm);
  } else if constexpr (T.ngapConds != 0) {
    if (arm == kNoRow) arm = ll_read_stopped<T>(c, decision);
  }
  ll_episode_cache<T>().work.tokens += episode.take_steps();
  return arm;
}

// The entry point the generated code calls.
template <const auto& T>
inline std::uint16_t ll_predict(LlContext& c, std::uint16_t decision) {
  if (c.give) return kNoRow;
  const LlTable::Decision& dec = T.decisions[decision];
  if (const std::uint16_t planned = ll_planned<T>(c, decision);
      planned != kLlUnplanned) {
    if constexpr (T.ngapConds != 0)
      if (planned == kNoRow) return ll_read_stopped<T>(c, decision);
    return planned;
  }
  if (dec.how == LlPredict::staged) {
    const std::uint16_t a = T.staged(c, decision);
    if (a != kNoRow) {
      ll_plan_went<T>(c, decision, a);
      return a;
    }
  }
  return ll_episode<T>(c, decision);
}

}  // namespace cursiv::engine
