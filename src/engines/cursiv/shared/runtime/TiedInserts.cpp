// The order of the terminals a repair names is the same sort for every
// language.
#include "cursiv/shared/runtime/TiedInserts.hpp"

#include <algorithm>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"

namespace cursiv::engine {

void TiedInserts::order(std::vector<ElementId>& terms) {
  std::ranges::sort(terms);
}

}  // namespace cursiv::engine
