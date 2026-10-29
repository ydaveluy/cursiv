// The rules a grammar reaches from its three roots, in the order reached:
// that order is the symbol numbering.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/core/grammar/GrammarImage.hpp"

namespace cursiv {

struct ReachedRules {
  // The rules read as themselves.
  std::vector<std::uint32_t> visible;
};

// `entry`, `ignored` and `hidden`, closed over the rules each body names, as
// the pipeline resolved the bodies. A rule a condition names is read there
// as text, never as a call: it is reached, and not visible.
ReachedRules reached_rules(GrammarImage& image);

}  // namespace cursiv
