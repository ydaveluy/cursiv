// The LR(1) automaton of a lowered grammar: the states of one core are kept
// apart where their lookaheads change a cell or a token (IELR), then joined
// wherever that changes neither.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/engine/parser/Bits.hpp"
#include "cursiv/engine/grammar/Cfg.hpp"

namespace cursiv::engine::gen {

struct LrState {
  // Terminal, target; ascending.
  std::vector<std::pair<std::uint16_t, std::uint32_t>> shifts;
  // Production, the terminals it is reduced on; ascending. Reducing
  // production 0 accepts. A terminal with more than one action is a cell the
  // text decides.
  std::vector<std::pair<std::uint32_t, Bits>> reduces;
  // Nonterminal index, target; ascending.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> gotos;
  // Its kernel's items: production, dot.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> kernel;
  // The terminals the tests of its productions name, and those the parse
  // reads after a test that names tokens: its lexer reads them, whatever
  // acts on them.
  std::vector<std::uint16_t> named;
  std::vector<std::uint16_t> follow;
  // Whether a bar stands in it: it reads the token that comes, whatever it
  // does with it.
  bool barred = false;
  // The trivia scopes its tokens are read under, a bit a scope; per terminal
  // it shifts, those of that token; per production it reduces, those of the
  // token after it, ascending.
  std::uint64_t scopes = 1;
  std::vector<std::pair<std::uint16_t, std::uint64_t>> governs;
  std::vector<std::pair<std::uint32_t, std::uint64_t>> behind;
  // The terminals a merge of contexts may have given it, which may stand
  // where a terminal it reads does: its stack may not take them. Ascending.
  std::vector<std::uint16_t> doubted;
};

struct LrAutomaton {
  // State 0 is the start.
  std::vector<LrState> states;
  // Per condition of the grammar, the state what it reads is read from.
  std::vector<std::uint32_t> conditions;
};

// The left-corner component of each nonterminal, past any given leading conditions.
std::vector<std::uint32_t> left_components(const Cfg& cfg, std::span<const std::uint32_t> prefix = {});

// Where a production's first token may come after symbols that may read
// nothing, and its caller reads otherwise, each of them is told apart as it
// reads something or nothing: the token is then the first or not.
void split_leads(Cfg& cfg);

// Per trivia scope, `Region::shadows`; per test of the token that comes,
// `Test::shadows`. `reads` as `lex_rivals` gives it.
void shadow_tests(Cfg& cfg, const std::vector<RivalRow>& reads);

// A test that only refuses tokens, none of which the symbols after it begin
// with, leaves the productions that use it as `Production::bars`: what comes
// after it is then read alike with it and without it.
void bar_tests(Cfg& cfg);

// The tokens that may read the text of a trivia passed under `scopes`, a bit
// a scope.
Bits trivia_shadows(const Cfg& cfg, std::uint64_t scopes);

// `takes` as `lex_rivals` gives it; `always`: the trivia every state reads.
// A nonzero `maxStates` bounds refinement to the hottest cells.
LrAutomaton build_lr1(const Cfg& cfg, const std::vector<RivalRow>& takes, const Bits& always,
                      std::uint32_t maxStates = 0);

}  // namespace cursiv::engine::gen
