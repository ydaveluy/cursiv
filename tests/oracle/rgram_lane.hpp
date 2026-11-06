// One random grammar's lane: its documents read by the generated parser and
// by the oracle, compared verdict by verdict and, where the text has one
// tree, tree by tree. A parse that does not finish is stopped by a watchdog
// that names its document.
#pragma once

namespace cursiv {

class Parser;
struct AstReflection;

// What a lane answers when it agrees on every document but judged none in the
// language, or no tree where its grammar builds them. The long run's tests
// skip on this code.
inline constexpr int kRgramUnjudged = 77;

// Zero where the parser and the oracle agree on every document and judged some
// in the language, one where they disagree, `kRgramUnjudged` where they agree
// on too little to tell.
int rgram_lane(unsigned seed, const Parser& parser,
               const AstReflection& reflection, unsigned documents = 40);

}  // namespace cursiv
