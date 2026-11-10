#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// The generated tables a `Grammar` implementation reads: one answer per record
// entry and one row per terminal.

namespace cursiv {

struct AstMetaData;

namespace detail {

// A terminal's name and spellings: a literal has one spelling and no name, a
// terminal that reads a choice of words has both, and a pattern terminal has a
// name only.
struct GrammarTerminal {
  std::string_view name;
  std::string_view doc;
  bool folded = false;
  std::span<const std::string_view> spellings{};
};

}  // namespace detail

struct GrammarMetaData {
  using AskFn = bool (*)(std::string_view);

  // What an entry answers; entries that answer the same share one.
  struct Answer {
    AskFn writes = nullptr;
    AskFn fills = nullptr;
    // The member the entry is named by and the rule its conversion names:
    // indices of `names`, 0 for none.
    std::uint16_t feature = 0;
    std::uint16_t rule = 0;
    // The class of the entry's member, which the tree does not know before the
    // record is open: an index of `ast`'s types, or past them for none.
    std::uint16_t type = 0xFFFF;
    // The hover text of the word the entry matches: an index of `names`, 0 for
    // none.
    std::uint16_t doc = 0;
    // A data rule converts what the entry's record spans.
    bool dataRule = false;
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

  const detail::GrammarTerminal* terminals = nullptr;
  std::uint16_t terminalCount = 0;
};
}  // namespace cursiv
