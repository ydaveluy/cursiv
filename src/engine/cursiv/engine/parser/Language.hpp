// A language as its generated source states it: the tables of its parser, of
// its tree builder and of its grammar's answers, all constant.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/engine/ast/AstBuilder.hpp"
#include "cursiv/engine/grammar/GrammarWord.hpp"
#include "cursiv/engine/parser/Tables.hpp"

namespace cursiv::engine {

// What a record's element says of the grammar. `feature` and `rule` are
// numbers of `GrammarFacts::names`, 0 for none: the member the record writes
// first, and the rule its text is converted by. The members it writes are
// those of `GrammarFacts::members` from `writes` on: into the node around it
// up to `fills`, then into its owners, outermost first, separated by 0.
struct ElementFacts {
  std::uint16_t feature = 0;
  std::uint16_t rule = 0;
  std::uint32_t writes = 0;
  std::uint32_t fills = 0;
  std::uint32_t end = 0;
  // A data rule converts what the record spans.
  bool dataRule = false;
  // A named terminal read the record's leaf.
  bool terminal = false;
  // 0 means exact, 1 accepts any case, and n >= 2 selects foldedSets[n - 2].
  std::uint16_t fold = 0;
  // The text written on the use of a word the record's leaf reads, where the
  // word's uses say several.
  std::string_view doc;
};

// A terminal as an editor names it: a rule's name, a literal's spelling.
struct TerminalFacts {
  std::string_view name;
  std::string_view doc;
  std::span<const std::string_view> spellings{};
};

struct GrammarFacts {
  const ElementFacts* elements = nullptr;
  std::uint32_t elementCount = 0;
  const std::uint16_t* members = nullptr;
  // Every name an element gives, the empty one first.
  const std::string_view* names = nullptr;
  const TerminalFacts* terminals = nullptr;
  std::uint16_t terminalCount = 0;
  std::span<const GrammarWord> words;
  const std::span<const FoldedWord>* foldedSets = nullptr;
  const Tables* lexer = nullptr;
};

struct Language {
  const Tables& tables;
  const AstTables& ast;
  const GrammarFacts& grammar;
};

}  // namespace cursiv::engine
