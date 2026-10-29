#include "cursiv/test/DecodedSemanticTokens.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectationError.hpp"
#include "cursiv/test/ExpectedBase.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "cursiv/test/ReplacedIndices.hpp"

namespace cursiv::test {

DecodedSemanticTokensWithRanges highlightHelper(
    const LspServices& services, std::string text,
    const ParseHelperOptions& options) {
  if (services.semanticTokenProvider == nullptr)
    throw ExpectationError("cursiv: no semantic token provider provided!");

  const ReplacedIndices replaced = replaceIndices({.text = std::move(text)});
  const std::shared_ptr<Document> document =
      parseHelper(services, replaced.output, options);
  const lsp::SemanticTokens encoded =
      services.semanticTokenProvider->semanticHighlight(*document, kNoCancel);

  DecodedSemanticTokensWithRanges out;
  out.ranges = replaced.ranges;
  const TextDocument& textDocument = document->textDocument();
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  for (std::size_t at = 0; at + 4 < encoded.data.size(); at += 5) {
    const auto deltaLine = static_cast<std::uint32_t>(encoded.data[at]);
    line += deltaLine;
    if (deltaLine != 0) character = 0;
    character += static_cast<std::uint32_t>(encoded.data[at + 1]);
    const auto length = static_cast<std::uint32_t>(encoded.data[at + 2]);
    const TextOffset begin = textDocument.offsetAt(Position{line, character});
    const TextOffset end =
        textDocument.offsetAt(Position{line, character + length});
    out.tokens.emplace_back(
        begin, static_cast<SemanticTokenType>(encoded.data[at + 3]),
        static_cast<std::uint32_t>(encoded.data[at + 4]),
        textDocument.getText().substr(begin, end - begin));
  }
  return out;
}

void expectSemanticToken(
    const DecodedSemanticTokensWithRanges& tokensWithRanges,
    const DecodedTokenOptions& options) {
  const auto& [begin, end] = tokensWithRanges.ranges[options.rangeIndex];
  std::size_t found = 0;
  for (const DecodedSemanticToken& token : tokensWithRanges.tokens)
    if (token.tokenType == options.tokenType && token.offset == begin &&
        token.offset + token.text.size() == end)
      ++found;
  expectedFunction(std::to_string(found), "1",
                   "expected one token with the specified options but found " +
                       std::to_string(found));
}

}  // namespace cursiv::test
