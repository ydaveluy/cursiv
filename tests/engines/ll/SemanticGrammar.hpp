#pragma once

// Semantic regression entries with the same whitespace and word-boundary policy.
#include "engines/ll/FoldOrderGrammar.hpp"
#include "engines/ll/DataTextGrammar.hpp"
#include "engines/ll/CreationBoundaryGrammar.hpp"
#include "engines/ll/KeywordRelayGrammar.hpp"
#include "engines/ll/SkipConfigsGrammar.hpp"
#include "engines/ll/TextOrderGrammar.hpp"
#include "engines/ll/CountedScopeGrammar.hpp"
#include "engines/ll/ReservedWordGrammar.hpp"
#include "engines/ll/ReservedAssertGrammar.hpp"

#include "engines/ll/CaretAgreementGrammar.hpp"
#include "engines/ll/CaretDepthGrammar.hpp"
#include "engines/ll/CaretGuardGrammar.hpp"
#include "engines/ll/IndirectLoopGrammar.hpp"

namespace tllsemantic {
struct Ll : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<(lit<"fold:"> + ref<tfoldorder::Base::Root>) |
                     (lit<"data:"> + ref<tlldatatext::Base::Root>) |
                     (lit<"creation:"> + ref<tllcreation::Combined::Root>) |
                     (lit<"relay:"> + ref<tkwrelay::Suite::Root>) |
                     (lit<"skip:"> + ref<tskipcfg::Wide::Root>) |
                     (lit<"order:"> + ref<tlltextorder::Combined::Root>) |
                     (lit<"scope:"> + ref<tscope::Base::Root>) |
                     (lit<"reserved:"> + ref<tllreserved::Base::Root>) |
                     (lit<"assert:"> + ref<tllreservedassert::Choice::Root>) |
                     (lit<"agree:"> + ref<tllcaretagree::Base::Root>) |
                     (lit<"depth:"> + ref<tcaretdepth::Base::Root>) |
                     (lit<"guard:"> + ref<tllcaretguard::Base::Root>) |
                     (lit<"loop:"> + ref<tindirectloop::Combined::Root>)> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace tllsemantic
