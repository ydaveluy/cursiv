// The rounds of a condition's repetition with no maximum. A condition tested
// at each token of a long run reads the run again from each token; the parse
// keeps, by round, whether the rest of the condition held and how far that
// read, so each round of a run is read once per document.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/PredicateState.hpp"
#include "cursiv/engines/shared/table/FlatMap.hpp"

namespace cursiv {

// What a condition answered from one round of a run, and how far it read.
struct PredicateRunAnswer {
  TextOffset reach = 0;
  bool holds = false;
};

// The answers of one document's runs, by run and position of the round.
class PredicateRuns {
 public:
  static std::uint64_t key(std::uint32_t run, TextOffset at) {
    return std::uint64_t{run} << 32 | at;
  }
  const PredicateRunAnswer* find(std::uint64_t key) { return _known.find(key); }
  void keep(std::uint64_t key, PredicateRunAnswer answer) {
    _known.emplace(key).first = answer;
  }

 private:
  FlatMap<PredicateRunAnswer> _known;
};

// How the generated condition reads one round of a repetition.
struct PredicateRound {
  // The cursor a round reads from: past the trivia between rounds where
  // there is some, with the token there read in the run's mode.
  PredicateCursor (*start)(const char*, TextOffset, PredicateCursor,
                           TextOffset*);
  // The cursors the round's item leaves.
  PredicateState (*step)(const char*, TextOffset, PredicateState, TextOffset*);
  // Trivia stands between rounds: a round's reading is the cursor before it.
  bool between;
};

// Whether the rest of a condition holds from the cursors it reads.
using PredicateRest = bool (*)(const char*, TextOffset, PredicateState,
                               TextOffset*);

// The rest of a condition that is nothing more: whether any cursor is left.
inline bool predicate_some(const char*, TextOffset, PredicateState in,
                           TextOffset*) {
  return !in.empty();
}

// Whether `rest` holds after some rounds of a repetition with no maximum,
// whose free rounds start from `cur`. `runs` keeps the answers of rounds a
// lone cursor reaches, under `run`, the repetition's place in its condition;
// without it every round is read. What is read and answered is the same
// either way.
bool predicate_run(PredicateRuns* runs, std::uint32_t run,
                   const PredicateRound& round, PredicateRest rest,
                   const char* src, TextOffset len, PredicateState cur,
                   TextOffset* seen);

}  // namespace cursiv
