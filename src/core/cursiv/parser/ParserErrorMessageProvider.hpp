#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/text/TextOffset.hpp"

// How a syntax error reads. The parser states facts in a `ParseDiagnostic`;
// this service words them, one method per kind of error, so a language
// rewords only the kinds it wants.

namespace cursiv {

struct Grammar;

class ParserErrorMessageProvider {
 public:
  virtual ~ParserErrorMessageProvider() noexcept = default;

  // The sentence for `diagnostic`, from the method its kind names. A
  // conversion's sentence is its own, and comes back as it is. `text` is the
  // document the diagnostic's offsets index.
  [[nodiscard]] std::string buildMessage(const Grammar& grammar,
                                         const ParseDiagnostic& diagnostic,
                                         std::string_view text) const;

  // The repairs; `grammar` is the one the diagnostic's elements belong to.
  //
  // A missing construct was assumed: nothing was read.
  [[nodiscard]] virtual std::string buildInsertedMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const = 0;
  // Text skipped to reach a point the parse could go on from.
  [[nodiscard]] virtual std::string buildDeletedMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const = 0;
  // Text read as the expected construct: a misspelling.
  [[nodiscard]] virtual std::string buildMismatchTokenMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const = 0;
  // The text ended before an expected construct.
  [[nodiscard]] virtual std::string buildIncompleteMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const = 0;
  // A span where the parser hit its work limit: the reading or the repairs
  // around it may not be the best.
  [[nodiscard]] virtual std::string buildBoundedMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const = 0;

  // What the parser could not repair: the text after what the entry rule
  // read, from `end` on.
  [[nodiscard]] virtual std::string buildNotAllInputParsedMessage(
      std::string_view text, TextOffset end) const = 0;
  // An entry rule that never completed.
  [[nodiscard]] virtual std::string buildUnparsedMessage(
      std::string_view text) const = 0;
  // A parse no tree could be built from.
  [[nodiscard]] virtual std::string buildNoTreeMessage(
      std::string_view text) const = 0;
};

// The spellings of what the repair expected, the first named first, at most
// `limit` of them: what a message quotes and what a quick fix writes.
[[nodiscard]] std::vector<std::string_view> expected_spellings(
    const Grammar& grammar, const ParseDiagnostic& diagnostic,
    std::size_t limit);

}  // namespace cursiv
