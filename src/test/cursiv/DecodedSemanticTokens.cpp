#include "cursiv/DecodedSemanticTokens.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/ReplacedIndices.hpp"
#include "cursiv/semantic/SemanticTokenProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/Position.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

DecodedSemanticTokensWithRanges highlightHelper(
    const LspServices& services, std::string text,
    const ParseHelperOptions& options) {
  ReplacedIndices replaced = replaceIndices({.text = std::move(text)});
  const std::shared_ptr<Document> document =
      parseHelper(services, std::move(replaced.output), options);
  // The document stays in the workspace, as a `parseHelper` one does, unless
  // the provider throws.
  Disposable added = detail::documentDisposable(services, document);
  const detail::DisposeOnThrow cleanup(added);
  return decodeTokens(services, *document, std::move(replaced.ranges));
}

DecodedSemanticTokensWithRanges decodeTokens(
    const LspServices& services, const Document& document,
    std::vector<std::pair<TextOffset, TextOffset>> ranges,
    const CancellationToken& cancel) {
  if (services.semanticTokenProvider == nullptr)
    throw ExpectationError("cursiv: no semantic token provider provided!");

  lsp::SemanticTokensParams params;
  params.textDocument.uri = lsp::Uri::parse(document.uri);
  const SemanticTokenProvider& provider = *services.semanticTokenProvider;
  const lsp::SemanticTokens encoded =
      provider.semanticHighlight(document, params, cancel);
  const lsp::Array<lsp::String> types =
      provider.semanticTokensOptions().legend.tokenTypes;

  // Five integers per token: a group cut short is a fault of the provider.
  expectedFunction(std::to_string(encoded.data.size() % 5), "0",
                   "the semantic tokens are not whole groups of five integers");

  DecodedSemanticTokensWithRanges out;
  out.ranges = std::move(ranges);
  const TextDocument& textDocument = document.textDocument();
  if (!encoded.data.empty())
    out.source = std::make_shared<const std::string>(textDocument.getText());
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
    const auto type = static_cast<std::size_t>(encoded.data[at + 3]);
    out.tokens.emplace_back(
        begin, type < types.size() ? types[type] : std::string(),
        static_cast<std::uint32_t>(encoded.data[at + 4]),
        std::string_view(*out.source).substr(begin, end - begin));
  }
  return out;
}

namespace {
using TokenRange = std::pair<TextOffset, TextOffset>;

void check_tokens(std::span<const DecodedSemanticToken> tokens,
                  std::span<const TokenRange> ranges, std::string_view type,
                  std::optional<std::uint32_t> modifiers) {
  if (ranges.empty()) throw ExpectationError("no semantic token range marked");
  std::map<TokenRange, std::size_t> counts;
  for (const auto& range : ranges) counts.try_emplace(range, 0);
  for (const auto& token : tokens) {
    if (token.tokenType != type ||
        (modifiers && token.tokenModifiers != *modifiers))
      continue;
    const auto end = token.offset + token.text.size();
    if (end > kMaxDocument) continue;
    if (auto it = counts.find({token.offset, static_cast<TextOffset>(end)});
        it != counts.end())
      ++it->second;
  }
  for (const auto& [range, count] : counts)
    expectedFunction(std::to_string(count), "1",
                     std::string(type) + " token at bytes " +
                         std::to_string(range.first) + ".." +
                         std::to_string(range.second));
}
}  // namespace

void DecodedSemanticTokensWithRanges::expectAll(
    std::string_view type, std::optional<std::uint32_t> modifiers) const {
  check_tokens(tokens, ranges, type, modifiers);
}

void expectSemanticToken(const DecodedSemanticTokensWithRanges& decoded,
                         const DecodedTokenOptions& options) {
  if (auto* range = detail::markerAt(decoded.ranges, options.rangeIndex))
    check_tokens(decoded.tokens, {range, 1}, options.tokenType,
                 options.tokenModifiers);
}

}  // namespace cursiv::test
