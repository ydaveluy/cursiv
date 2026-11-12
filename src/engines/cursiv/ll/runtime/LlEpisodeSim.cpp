// What a parse does once to the walk of its predictions: make it, give its
// memory back, drop it.
#include "cursiv/ll/runtime/LlEpisodeSim.hpp"

namespace cursiv::engine {

LlEpisodeSim::LlEpisodeSim(const LlAtnView& atn, LlReturnPool& pool,
                           bool tracked)
    : _atn(atn), _pool(pool), _tracked(tracked) {}

LlEpisodeSim::~LlEpisodeSim() = default;

void LlEpisodeSim::trim() {
  const auto each = [](auto&... x) { (x.trim(), ...); };
  each(_arms, _lexes, _reduces, _seen, _at, _held, _crossed, _calls, _flat,
       _below, _relatedProcessed, _relatedFlags, _relatedBaseCount,
       _relatedBases);
}

}  // namespace cursiv::engine
