// What every semantic token provider shares: the legend, the wire encoding, and
// the last answer per document that `full/delta` sends its edits from. A
// language says only which spans get which colour, in `computeHighlighting`.
#pragma once

#include <lsp/types.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/semantic/SemanticTokenProvider.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

// The token types and modifiers the protocol predefines, in the order a
// provider declares them by default.
inline constexpr std::array<std::string_view, 23> kSemanticTokenTypes = {
    "class",         "comment",   "enum",     "enumMember", "event",
    "function",      "interface", "keyword",  "macro",      "method",
    "modifier",      "namespace", "number",   "operator",   "parameter",
    "property",      "regexp",    "string",   "struct",     "type",
    "typeParameter", "variable",  "decorator"};
inline constexpr std::array<std::string_view, 10> kSemanticTokenModifiers = {
    "abstract",   "async",      "declaration",   "defaultLibrary",
    "definition", "deprecated", "documentation", "modification",
    "readonly",   "static"};

// One coloured span of the document's text: `type` indexes the provider's
// `tokenTypes()`, and `modifiers` holds one bit per index of its
// `tokenModifiers()`.
struct SemanticToken {
  TextOffset begin = 0;
  TextOffset end = 0;
  std::uint32_t type = 0;
  std::uint32_t modifiers = 0;
};

using SemanticTokenAcceptor = function_ref<void(SemanticToken)>;

class AbstractSemanticTokenProvider : public SemanticTokenProvider {
 public:
  /**
   * The names the type indices and modifier bits stand for: the protocol's
   * own by default. A language that adds its own names appends them, so that
   * the languages of one server still agree on the indices they share.
   */
  [[nodiscard]] virtual std::span<const std::string_view> tokenTypes() const;
  [[nodiscard]] virtual std::span<const std::string_view> tokenModifiers()
      const;

  /**
   * The legend of the two lists, with `full` and `range`. No `delta`: every
   * answer is computed whole, so its edits save only the transfer. A language
   * that wants them sets `full.delta`; each open document then keeps its last
   * answer.
   */
  [[nodiscard]] lsp::SemanticTokensOptions semanticTokensOptions()
      const override;

  [[nodiscard]] lsp::SemanticTokens semanticHighlight(
      const Document& document, const lsp::SemanticTokensParams& params,
      const CancellationToken& token = {}) const override;
  [[nodiscard]] lsp::SemanticTokens semanticHighlightRange(
      const Document& document, const lsp::SemanticTokensRangeParams& params,
      const CancellationToken& token = {}) const override;
  [[nodiscard]] lsp::OneOf<lsp::SemanticTokens, lsp::SemanticTokensDelta>
  semanticHighlightDelta(const Document& document,
                         const lsp::SemanticTokensDeltaParams& params,
                         const CancellationToken& token = {}) const override;

  // The index of a type and the bit of a modifier, by name. Nothing, and no
  // bit, for a name the legend does not hold.
  [[nodiscard]] std::optional<std::uint32_t> tokenType(
      std::string_view name) const;
  [[nodiscard]] std::uint32_t tokenModifier(std::string_view name) const;

 protected:
  /**
   * Accepts every token of `document` touching `[begin, end)`, in any order.
   * A token outside the window, or empty, is dropped. Where tokens overlap,
   * the one starting first wins, then the longer, then the first accepted. A
   * token over several lines is sent as one per line. Precondition: called
   * under the workspace read lock.
   */
  virtual void computeHighlighting(const Document& document, TextOffset begin,
                                   TextOffset end, SemanticTokenAcceptor accept,
                                   const CancellationToken& token) const = 0;

 private:
  [[nodiscard]] std::vector<lsp::Uint> encode(
      const Document& document, TextOffset begin, TextOffset end,
      const CancellationToken& token) const;
  [[nodiscard]] bool answersDelta() const;
  // Keeps `data` as the document's last answer under a new id, and gives
  // back the previous answer if `previousId` names it. Forgets the answers of
  // documents no longer open.
  std::optional<std::vector<lsp::Uint>> keep(
      const Document& document, const std::vector<lsp::Uint>& data,
      std::string& id, std::string_view previousId = {}) const;

  struct Answer {
    std::string id;
    std::vector<lsp::Uint> data;
  };
  mutable std::mutex _mutex;
  mutable std::unordered_map<std::string, Answer> _answers;
};

}  // namespace cursiv
