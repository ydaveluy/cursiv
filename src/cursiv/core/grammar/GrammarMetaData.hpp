#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// What one compiled grammar answers about its records and its tokens, as data:
// a question per entry of the built table, and a row per terminal -- the tables
// an implementation of `Grammar` answers from, written by the generator.

namespace cursiv {

struct AstMetaData;

// A terminal is told by its name and spelled by its words, each handed over
// whole: a literal has no name and one word, a terminal that reads a choice of
// words has both, and one whose text is matched has a name alone.
struct GrammarTerminal {
  std::string_view name;
  std::string_view doc;
  bool folded = false;
  std::span<const std::string_view> spellings{};
};

struct GrammarMetaData {
  using AskFn = bool (*)(std::string_view);

  // What an entry answers; entries that answer alike share one.
  struct Answer {
    AskFn writes = nullptr;
    AskFn fills = nullptr;
    // The member the entry is named by and the rule its conversion names:
    // indices of `names`, 0 for none.
    std::uint16_t feature = 0;
    std::uint16_t rule = 0;
    // The class the entry's member belongs to, which no tree holds before
    // the record is open: an index of `ast`'s types, past them for none.
    std::uint16_t type = 0xFFFF;
  };

  const Answer* answers = nullptr;
  // Each entry's index in `answers`.
  const std::uint16_t* recordAnswers = nullptr;
  // Every name an answer gives, the empty one first.
  const std::string_view* names = nullptr;
  // The AST tables `Answer::type` indexes.
  const AstMetaData* ast = nullptr;
  // The narrowing of the site each record writes; null for a grammar that
  // narrows nowhere.
  const std::uint8_t* narrows = nullptr;
  std::uint32_t recordCount = 0;

  const GrammarTerminal* terminals = nullptr;
  std::uint16_t terminalCount = 0;
};
}  // namespace cursiv
