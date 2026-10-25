// Learned prediction answers belong to one public parse, including AST
// conversion. An AST converter may reenter the same parser: its child starts
// empty and restores its caller's state when it exits or unwinds.
#pragma once

#include <cstdint>
#include <optional>

#include "cursiv/engines/ll/runtime/ll_predictor.hpp"

namespace cursiv {

template <const auto& T>
class LlPredictionSession {
 public:
  LlPredictionSession() noexcept
      : _outerEpisodes(ll_episode_session_slot<T>()),
        _stagedFrom(ll_staged_reads<T>()) {
    ll_episode_session_slot<T>() = &_episodes;
  }
  ~LlPredictionSession() {
    // An episode kept reads in the cache that goes with this session.
    if (_episodes) ll_episode_engine<T>().release(&*_episodes);
    ll_episode_session_slot<T>() = _outerEpisodes;
    ll_staged_reads<T>() = _stagedFrom;
  }
  LlPredictionSession(const LlPredictionSession&) = delete;
  LlPredictionSession& operator=(const LlPredictionSession&) = delete;
  LlPredictionSession(LlPredictionSession&&) = delete;
  LlPredictionSession& operator=(LlPredictionSession&&) = delete;

  // What the predictions of this session have read so far.
  LlPredictionWork work() const {
    LlPredictionWork sum{.tokens = ll_staged_reads<T>() - _stagedFrom};
    if (_episodes) {
      sum.tokens += _episodes->work.tokens;
      sum.readings += _episodes->work.readings;
    }
    return sum;
  }

 private:
  // The common LL(1) path never allocates a prediction cache.
  std::optional<LlEpisodeCache> _episodes;
  std::optional<LlEpisodeCache>* _outerEpisodes;
  std::uint64_t _stagedFrom;
};

}  // namespace cursiv
