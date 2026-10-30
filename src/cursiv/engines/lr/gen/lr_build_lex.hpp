// The lexer a table carries: the alphabet its modes are written in, the mode
// each state reads, and the boundary that decides what a mode wants.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/gen/LrItem.hpp"
#include "cursiv/engines/lr/gen/LrBuildTable.hpp"
#include "cursiv/engines/shared/gen/LexAlphabet.hpp"
#include "cursiv/engines/shared/gen/LexModes.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"

namespace cursiv {

LexAlphabet lr_lex_alphabet(LrBuildTable& A);

// What each section's grammar reads, by the section's own configuration.
std::vector<DynSet> lr_section_reads(const LrBuildCfg& g);

// Per state, the terminals the conditions it asks read first.
std::vector<DynSet> lr_state_guard_firsts(const LrBuildTable& A,
                                          const LrPoolRows<LrItem>& clos,
                                          const std::vector<unsigned>& pick,
                                          const std::vector<DynSet>& firsts);

// `flagged`, per state, the terminals the lexer may read over one a caller of
// the state takes; empty where no state has any.
LexModes lr_lex_mode_sets(LrBuildTable& A, const std::vector<std::uint8_t>& cfg,
                          const std::vector<DynSet>& guardFirsts,
                          const std::vector<DynSet>& impliedFirsts,
                          const std::vector<DynSet>& flagged);

// The automaton of every mode and the reserve it moves, which read no parse
// state; what each DFA state's words reserve is the candidates' to read.
std::vector<DynSet> lr_lex_automaton(LrBuildTable& A, const LexAlphabet& alpha,
                                     const LexModes& modes);
// The modes whose states read a rival of an acceptance, over the states as
// the passes left them.
void lr_lex_candidates(LrBuildTable& A, const LexAlphabet& alpha,
                       const LexModes& modes,
                       const std::vector<DynSet>& wordAt);
// A terminal a condition may refuse is read only where the condition lets the
// stack take it. Where the lexer would read it over another acceptance of its
// mode -- the same text elected or reserved for it, or a longer one -- the mode
// flags it, so the position is read again without it once no stack takes it.
void lr_lex_condition_flags(LrBuildTable& A);

std::vector<std::uint8_t> lr_state_cfgs(LrBuildTable& A,
                                        const LrPoolRows<LrItem>& clos,
                                        const std::vector<unsigned>& pick,
                                        const std::vector<DynSet>& first);

// The fallback election at a refused boundary, and the distinct successors
// a typo can try.
void lr_derive_lexical(const LrBuildTable& a, LrDerivedRows& rows);

}  // namespace cursiv
