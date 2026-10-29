#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/core/syntax-tree/CstNode.hpp"

// What a grammar answers about the records of its own trees, with the
// grammar erased. A `CstNode` carries a stamp only the engine that wrote it
// decodes; this is what decodes it, reached through the document that owns
// the node. Its one implementation answers from a `GrammarMetaData`, one per
// compiled grammar.

namespace cursiv {

struct Grammar {
  constexpr virtual ~Grammar() = default;

  // A CST record does not name a row of the model: it names what it owes the
  // tree. The two vocabularies answer different questions -- a row is a
  // position of the model, a record is a span and its effects -- so a reader
  // holding a record asks here and nowhere else.

  // Does the record owe that member to the region above it? Asked and not
  // read back as one name, because a run may owe several -- stacked
  // directives are several actions.
  constexpr virtual bool recordWrites(const CstNode&,
                                      std::string_view) const noexcept = 0;
  // ...and does it write that member into the node it opened? The two are
  // one question asked of two nodes: a record fills what it built and owes
  // what it did not, so a reader looking for "where was this member
  // written" tests its own record with the first and its children with the
  // second.
  constexpr virtual bool recordFills(const CstNode&,
                                     std::string_view) const noexcept = 0;
  // The rule whose value the record's span denotes: a terminal's own name, a
  // data rule's. Empty for a record that converts nothing.
  constexpr virtual std::string_view recordRule(
      const CstNode&) const noexcept = 0;
  // The member the record writes first -- the innermost directive standing
  // over the position, which is the one the token itself fills.
  constexpr virtual std::string_view recordFeature(
      const CstNode&) const noexcept = 0;

  // A continuation is a token the lexer could match, so it is named by its
  // terminal and not by a row above.

  // The bytes an editor would insert, each spelling whole: a literal is
  // spelled by the grammar, once, and a terminal that reads a choice of words
  // by each of them. Empty for a terminal whose text is matched and not
  // written. The id is the terminal, as the engine that generated this
  // numbers them -- not an element row.
  constexpr virtual std::span<const std::string_view> terminalSpellings(
      std::uint16_t) const noexcept = 0;
  // ...and what to call it where it has no one spelling.
  constexpr virtual std::string_view terminalName(
      std::uint16_t) const noexcept = 0;
  // The prose an author wrote over it, for a reader that offers it or hovers
  // over it. Empty where none was written, which is almost every token.
  constexpr virtual std::string_view terminalDoc(
      std::uint16_t) const noexcept = 0;
  // ...and the same prose reached from the bytes, for a reader holding a leaf:
  // a record says what it owes the tree, never which token matched it, and a
  // keyword is its spelling. Empty where the text spells no literal.
  constexpr virtual std::string_view spellingDoc(
      std::string_view) const noexcept = 0;
};

}  // namespace cursiv
