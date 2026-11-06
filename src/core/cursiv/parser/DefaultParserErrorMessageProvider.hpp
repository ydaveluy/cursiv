#pragma once

#include <string>
#include <string_view>

#include "cursiv/parser/ParserErrorMessageProvider.hpp"

// The sentences an editor shows by default. No second parse, and no state:
// the recovery corpus renders the same sentences an editor gets, so a wording
// that drifts is a line that moved in a reference.

namespace cursiv {

// Not `final`: a language derives it to reword some shapes and keep the rest.
class DefaultParserErrorMessageProvider : public ParserErrorMessageProvider {
 public:
  [[nodiscard]] std::string buildInsertedMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const override;
  [[nodiscard]] std::string buildDeletedMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const override;
  [[nodiscard]] std::string buildMismatchTokenMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const override;
  [[nodiscard]] std::string buildIncompleteMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const override;
  [[nodiscard]] std::string buildBoundedMessage(
      const Grammar& grammar, const ParseDiagnostic& diagnostic,
      std::string_view text) const override;

  [[nodiscard]] std::string buildNotAllInputParsedMessage(
      std::string_view text, TextOffset end) const override;
  [[nodiscard]] std::string buildUnparsedMessage(
      std::string_view text) const override;
  [[nodiscard]] std::string buildNoTreeMessage(
      std::string_view text) const override;
};

}  // namespace cursiv
