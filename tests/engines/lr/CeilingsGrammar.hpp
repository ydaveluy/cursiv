#pragma once

// Independent graph and recovery bounds share a generated language and lexicon.
#include "engines/lr/EpisodeGrammars.hpp"
#include "engines/lr/FoldGrammars.hpp"
#include "engines/lr/RankGrowthGrammar.hpp"
#include "engines/lr/WhereBlockGrammar.hpp"

namespace tceilings {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<(kw<"episode"> + ref<tepisode::Long::Root>) |
                     (kw<"fold"> + ref<tfold::G::Ternary>) |
                     (kw<"rank"> + ref<trankgrowth::G::Root>) |
                     (kw<"open"> + ref<twhere::Open::Root>) |
                     (kw<"take"> + ref<twhere::Taken::Root>)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tceilings
