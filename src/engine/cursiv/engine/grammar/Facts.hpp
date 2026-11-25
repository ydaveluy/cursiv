// A grammar as the engine reads it, as types: the stage its rules are read
// through and the actions it publishes.
#pragma once

#include "cursiv/engine/grammar/RuleRows.hpp"
#include "cursiv/engine/grammar/reached.hpp"
#include "cursiv/grammar/GrammarUtils.hpp"

namespace cursiv::engine {

// Keyed on the grammar alone, so the lists stay out of every name mangled
// over a view of it.
template <class G>
struct Facts {
  using resolved = detail::ResolveOverride<G>;
  using stage = detail::MaterializeCreate<resolved>;
  using rules = reached_t<stage>;
  using rows = typename AllRows<stage, rules>::type;
};

}  // namespace cursiv::engine
