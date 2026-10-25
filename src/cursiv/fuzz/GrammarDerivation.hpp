// The core value a document comes from: one walk of the declarative
// grammar. `text` and `tree` are both written by that same walk, so
// neither can be right about a document the other is wrong about.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/fuzz/AstExcused.hpp"

namespace cursiv::fuzzer {

struct GrammarToken {
  ElementId elem = kNoElement;  // the literal or terminal rule it spells
  std::string text;
  std::string sep;     // the trivia in front of it, empty for none
  bool glued = false;  // inside an adjacency island: no trivia may precede it
  std::uint32_t begin = 0, end = 0;
};

struct GrammarDerivation {
  std::vector<GrammarToken> tokens;
  std::string text;
  AstNode* tree = nullptr;  // the AST the grammar's directives build
  bool complete = false;    // the walk finished a sentence
  unsigned capHits = 0;     // ceilings met, never silent
  unsigned unrendered = 0;  // a terminal whose value could not be spelled
  unsigned styles = 0;      // bit per SpellStyle a terminal was spelled with
  // The members the walk could not write: a write it refuses is a limit of
  // this instrument, never an equality.
  std::vector<AstExcused> unwritten;
};

}  // namespace cursiv::fuzzer
