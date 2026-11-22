// Decodes the delta-encoded semantic tokens back into absolute offsets and
// legend names, so a check can assert on one token near a marked range.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/ParseHelperOptions.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"

namespace cursiv {
struct LspServices;
struct Document;
}  // namespace cursiv

namespace cursiv::test {

struct DecodedSemanticToken {
  TextOffset offset = 0;
  // The legend's name for the token's type.
  std::string tokenType{};
  std::uint32_t tokenModifiers = 0;
  // A slice of the decoded snapshot.
  std::string_view text{};
};

struct DecodedSemanticTokensWithRanges {
  std::vector<DecodedSemanticToken> tokens{};
  std::vector<std::pair<TextOffset, TextOffset>> ranges{};
  std::shared_ptr<const std::string> source{};

  void expectAll(std::string_view tokenType,
                 std::optional<std::uint32_t> modifiers = {}) const;
};

struct DecodedTokenOptions {
  std::size_t rangeIndex = 0;
  std::string tokenType{};
  std::optional<std::uint32_t> tokenModifiers{};
};

// `text` carries the markers; what is parsed is the text without them.
[[nodiscard]] DecodedSemanticTokensWithRanges highlightHelper(
    const LspServices& services, std::string text,
    const ParseHelperOptions& options = {});

[[nodiscard]] DecodedSemanticTokensWithRanges decodeTokens(
    const LspServices& services, const Document& document,
    std::vector<std::pair<TextOffset, TextOffset>> ranges,
    const CancellationToken& cancel = {});

// Exactly one token of that type covering the marked range.
void expectSemanticToken(
    const DecodedSemanticTokensWithRanges& tokensWithRanges,
    const DecodedTokenOptions& options);

}  // namespace cursiv::test
