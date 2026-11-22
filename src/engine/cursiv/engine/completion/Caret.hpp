// What may be written at a place in a text: the terminals the parser reads
// there once it has read the text before it.
#pragma once

#include <vector>

#include "cursiv/engine/parser/Language.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"

namespace cursiv::engine {

// Adds to `out` what `language` may read at `at` of the text of `cst`, the
// text resuming at `resume`: each terminal, with the member its token fills,
// the class of that member and whether its node begins there.
void next_features(const Language& language, const Cst& cst, TextOffset at, TextOffset resume,
                   std::vector<NextFeature>& out, const CancellationToken& cancelToken);

}  // namespace cursiv::engine
