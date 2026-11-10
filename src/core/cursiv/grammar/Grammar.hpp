#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/syntax-tree/CstNode.hpp"

// Type-erased questions about a grammar and the CST records its engine writes.
// Implemented from a `GrammarMetaData`.

namespace cursiv {

struct Grammar {
  constexpr virtual ~Grammar() = default;

  // Whether the record writes that member into the enclosing node. A query
  // rather than one name, since stacked directives write several members.
  constexpr virtual bool recordWrites(const CstNode&,
                                      std::string_view) const noexcept = 0;
  // Whether the record writes that member into the node it created.
  constexpr virtual bool recordFills(const CstNode&,
                                     std::string_view) const noexcept = 0;
  // The rule whose value the record's span denotes: a terminal's own name, a
  // data rule's. Empty for a record that converts nothing.
  constexpr virtual std::string_view recordRule(
      const CstNode&) const noexcept = 0;
  // Whether a data rule converts what the record spans. A data rule of one token
  // has no record of its own, so `recordRule` of its leaf names the terminal and
  // this still answers true.
  constexpr virtual bool recordIsDataRule(const CstNode&) const noexcept = 0;
  // The member the record writes first: the innermost directive over it, the
  // one the token itself fills.
  constexpr virtual std::string_view recordFeature(
      const CstNode&) const noexcept = 0;

  // The bytes an editor would insert: each spelling of a literal, or of a
  // terminal's words. Empty for a terminal never written. The id is the
  // engine's terminal number.
  constexpr virtual std::span<const std::string_view> terminalSpellings(
      std::uint16_t) const noexcept = 0;
  // The terminal's name, for one with no single spelling.
  constexpr virtual std::string_view terminalName(
      std::uint16_t) const noexcept = 0;
  // The doc text written on the terminal, for completion and hover. Empty
  // where none was written, or where two uses of the word have different text.
  constexpr virtual std::string_view terminalDoc(
      std::uint16_t) const noexcept = 0;
  // The same doc text, looked up by spelling, for a reader holding a leaf.
  // Empty where the text spells no literal.
  constexpr virtual std::string_view spellingDoc(
      std::string_view) const noexcept = 0;
  // Whether the grammar reads that literal in any letter case: a reader that
  // looks a keyword up by its spelling then takes every case of it.
  constexpr virtual bool spellingFolds(std::string_view) const noexcept = 0;
  // The doc text of the use a leaf matched, for a word whose uses have
  // different texts. Empty for any other word, which `spellingDoc` answers.
  constexpr virtual std::string_view recordDoc(
      const CstNode&) const noexcept = 0;
};

}  // namespace cursiv
