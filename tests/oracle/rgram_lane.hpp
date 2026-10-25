// One random grammar's lane: its documents read by the generated parser and
// by the oracle, compared verdict by verdict and, where the text has one
// tree, tree by tree. A parse that does not finish is stopped by a watchdog
// that names its document.
#pragma once

namespace cursiv {

class Parser;
struct AstReflection;

// Zero where the parser and the oracle agree on every document.
int rgram_lane(unsigned seed, const Parser& parser,
               const AstReflection& reflection, unsigned documents = 40);

}  // namespace cursiv
