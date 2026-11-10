#pragma once

#include <string>
#include <string_view>

#include "cursiv/parser/ParserErrorMessageProvider.hpp"

// The default sentences an editor shows. No second parse and no state. The
// recovery reports print the same sentences, so a wording change shows in
// their reference files.

namespace cursiv {

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
