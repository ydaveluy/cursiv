// Decodes the delta-encoded semantic tokens back into absolute offsets and
// legend names, so a check can assert on one token near a marked range.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/ParseHelperOptions.hpp"

namespace cursiv {
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct DecodedSemanticToken {
  TextOffset offset = 0;
  // The legend's name for the token's type.
  std::string tokenType{};
  std::uint32_t tokenModifiers = 0;
  // A slice of the document's own text: valid while the workspace holds it.
  std::string_view text{};
};

struct DecodedSemanticTokensWithRanges {
  std::vector<DecodedSemanticToken> tokens{};
  std::vector<std::pair<TextOffset, TextOffset>> ranges{};
};

struct DecodedTokenOptions {
  std::size_t rangeIndex = 0;
  std::string tokenType{};
};

// `text` carries the markers; what is parsed is the text without them.
[[nodiscard]] DecodedSemanticTokensWithRanges highlightHelper(
    const LspServices& services, std::string text,
    const ParseHelperOptions& options = {});

// Exactly one token of that type covering the marked range.
void expectSemanticToken(
    const DecodedSemanticTokensWithRanges& tokensWithRanges,
    const DecodedTokenOptions& options);

}  // namespace cursiv::test
