// A misspelling is repaired by reading its bytes again, never by rewriting
// them: a walk of the lexer's automaton with a bounded number of faults says
// how far a terminal the state expects would have read, and at what cost.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

struct Machine;
struct LexWork;

// What a word read as a terminal it does not resemble costs: its deletion
// and the terminal's insertion. What reading text again costs at least, the
// least any edit does.
inline constexpr std::uint32_t kReplaceCost = 16;
inline constexpr std::uint32_t kRereadCost = 4;

// A span from where the walk began, and the terminal it is read as.
struct Typo {
  TextOffset end = 0;
  std::uint16_t terminal = 0;
  std::uint16_t cost = 0;
  bool substitutes = false;
};

// The readings of the text at `at` as a terminal `state` acts on, each
// terminal and end once at its least cost: the cheapest first, then the
// longest. All branches and their guards share the same byte budget.
std::vector<Typo> typos(Machine& m, std::uint32_t state, TextOffset at, TextOffset plain, LexWork& work);

}  // namespace cursiv::engine
