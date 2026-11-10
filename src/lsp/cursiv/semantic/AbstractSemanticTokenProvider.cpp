#include "cursiv/semantic/AbstractSemanticTokenProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/text/Position.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv {

namespace {

// One counter for every provider: a document that changes language keeps a
// result id no other answer of its own ever carried.
std::atomic<std::uint64_t> last_result_id{0};

// The largest integer the protocol carries: its `uinteger` is 0 to 2^31 - 1.
constexpr lsp::Uint kMaxUinteger = 0x7FFFFFFFu;

// The five wire integers: `deltaLine` from the previous token's line,
// `deltaStart` from its column but only on the same line. Callers present
// tokens in source order, never overlapping.
void encode_token(std::vector<lsp::Uint>& data, lsp::Uint& prevLine,
                  lsp::Uint& prevChar, lsp::Uint line, lsp::Uint start,
                  lsp::Uint stop, const SemanticToken& token) {
  if (stop <= start) return;
  const lsp::Uint deltaLine = line - prevLine;
  data.push_back(deltaLine);
  data.push_back(deltaLine == 0 ? start - prevChar : start);
  data.push_back(stop - start);
  data.push_back(token.type);
  data.push_back(token.modifiers & kMaxUinteger);
  prevLine = line;
  prevChar = start;
}

// A token cannot cross a line: a multi-line span, such as a block comment, is
// sent as one token per line.
void emit(std::vector<lsp::Uint>& data, TextDocument::Cursor& cursor,
          lsp::Uint& prevLine, lsp::Uint& prevChar, const SemanticToken& token,
          const CancellationToken& cancel) {
  Position from = cursor.positionAt(token.begin);
  while (cursor.crossesLine(token.end)) {
    throw_if_cancelled(cancel);
    const Position end = cursor.lineEnd();
    encode_token(data, prevLine, prevChar, from.line, from.character,
                 end.character, token);
    from = cursor.nextLine();
  }
  const Position to = cursor.positionAt(token.end);
  encode_token(data, prevLine, prevChar, from.line, from.character,
               to.character, token);
}

// The integers that encode one token.
constexpr std::size_t kTokenWidth = 5;

// The one edit that turns `before` into `after`, between their common prefix
// and suffix; none when equal. Nothing when the protocol cannot express the
// edit's start or count: the whole answer goes instead.
[[nodiscard]] std::optional<lsp::SemanticTokensDelta> edits_between(
    const std::vector<lsp::Uint>& before, const std::vector<lsp::Uint>& after) {
  lsp::SemanticTokensDelta delta;
  const std::size_t shorter = std::min(before.size(), after.size());
  std::size_t prefix = 0;
  while (prefix < shorter && before[prefix] == after[prefix]) ++prefix;
  std::size_t suffix = 0;
  while (suffix < shorter - prefix &&
         before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
    ++suffix;
  if (prefix == before.size() && prefix == after.size()) return delta;
  const std::size_t deleted = before.size() - prefix - suffix;
  if (prefix > kMaxUinteger || deleted > kMaxUinteger) return std::nullopt;
  lsp::SemanticTokensEdit edit;
  edit.start = static_cast<lsp::Uint>(prefix);
  edit.deleteCount = static_cast<lsp::Uint>(deleted);
  if (after.size() - suffix > prefix)
    edit.data.emplace(after.begin() + static_cast<std::ptrdiff_t>(prefix),
                      after.end() - static_cast<std::ptrdiff_t>(suffix));
  delta.edits.push_back(std::move(edit));
  return delta;
}

[[nodiscard]] TextOffset text_end(const Document& document) {
  return static_cast<TextOffset>(document.textDocument().getText().size());
}

}  // namespace

std::span<const std::string_view> AbstractSemanticTokenProvider::tokenTypes()
    const {
  return kSemanticTokenTypes;
}

std::span<const std::string_view>
AbstractSemanticTokenProvider::tokenModifiers() const {
  return kSemanticTokenModifiers;
}

lsp::SemanticTokensOptions
AbstractSemanticTokenProvider::semanticTokensOptions() const {
  lsp::SemanticTokensOptions options;
  for (const std::string_view name : tokenTypes())
    options.legend.tokenTypes.emplace_back(name);
  for (const std::string_view name : tokenModifiers())
    options.legend.tokenModifiers.emplace_back(name);
  lsp::SemanticTokensFullDelta full;
  full.delta = false;
  options.full = std::move(full);
  options.range = true;
  return options;
}

std::optional<std::uint32_t> AbstractSemanticTokenProvider::tokenType(
    std::string_view name) const {
  const std::span<const std::string_view> types = tokenTypes();
  const auto at = std::ranges::find(types, name);
  if (at == types.end()) return std::nullopt;
  return static_cast<std::uint32_t>(at - types.begin());
}

std::uint32_t AbstractSemanticTokenProvider::tokenModifier(
    std::string_view name) const {
  const std::span<const std::string_view> modifiers = tokenModifiers();
  const auto at = std::ranges::find(modifiers, name);
  // A bit per modifier, below the protocol's 2^31: a modifier past the 31st
  // has no bit to stand for it.
  const auto index = at - modifiers.begin();
  if (at == modifiers.end() || index >= 31) return 0;
  return std::uint32_t{1} << index;
}

lsp::SemanticTokens AbstractSemanticTokenProvider::semanticHighlight(
    const Document& document, const lsp::SemanticTokensParams&,
    const CancellationToken& token) const {
  lsp::SemanticTokens answer;
  answer.data = encode(document, 0, text_end(document), token);
  // Kept only where a delta may be asked from it.
  if (answersDelta()) {
    std::string id;
    (void)keep(document, answer.data, id, token);
    answer.resultId = std::move(id);
  }
  return answer;
}

lsp::SemanticTokens AbstractSemanticTokenProvider::semanticHighlightRange(
    const Document& document, const lsp::SemanticTokensRangeParams& params,
    const CancellationToken& token) const {
  const TextDocument& text = document.textDocument();
  lsp::SemanticTokens answer;
  answer.data = encode(document, text.offsetAt(from_lsp(params.range.start)),
                       text.offsetAt(from_lsp(params.range.end)), token);
  return answer;
}

lsp::OneOf<lsp::SemanticTokens, lsp::SemanticTokensDelta>
AbstractSemanticTokenProvider::semanticHighlightDelta(
    const Document& document, const lsp::SemanticTokensDeltaParams& params,
    const CancellationToken& token) const {
  std::vector<lsp::Uint> data = encode(document, 0, text_end(document), token);
  std::string id;
  std::optional<lsp::SemanticTokensDelta> delta =
      keep(document, data, id, token, params.previousResultId);
  if (!delta.has_value()) {
    lsp::SemanticTokens whole;
    whole.resultId = std::move(id);
    whole.data = std::move(data);
    return whole;
  }
  delta->resultId = std::move(id);
  return std::move(*delta);
}

std::vector<lsp::Uint> AbstractSemanticTokenProvider::encode(
    const Document& document, TextOffset begin, TextOffset end,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  std::vector<SemanticToken> tokens;
  computeHighlighting(
      document, begin, end,
      [&](SemanticToken accepted) {
        // A token touches the window when it neither ends before it nor
        // starts after it; one cut by the edge is coloured whole.
        if (accepted.end > accepted.begin && accepted.begin < end &&
            accepted.end > begin)
          tokens.push_back(accepted);
      },
      token);
  // A stable sort, so tokens of one span keep their order and the first wins.
  // Only what precedes the sorted tail is sorted, then merged into it.
  const auto before = [](const SemanticToken& a, const SemanticToken& b) {
    if (a.begin != b.begin) return a.begin < b.begin;
    return a.end > b.end;
  };
  auto tail = tokens.end();
  if (tail != tokens.begin()) --tail;
  while (tail != tokens.begin() && !before(*tail, *(tail - 1))) --tail;
  std::ranges::stable_sort(tokens.begin(), tail, before);
  std::ranges::inplace_merge(tokens.begin(), tail, tokens.end(), before);

  std::vector<lsp::Uint> data;
  if (tokens.empty()) return data;
  data.reserve(tokens.size() * kTokenWidth);
  TextDocument::Cursor cursor(document.textDocument(), tokens.front().begin);
  lsp::Uint prevLine = 0;
  lsp::Uint prevChar = 0;
  // The end of the last token sent. Zero is the right start: nothing can
  // begin before the first byte, so the first token is never dropped by it.
  TextOffset emitted = 0;
  for (const SemanticToken& t : tokens) {
    throw_if_cancelled(token);
    if (t.begin < emitted) continue;
    emit(data, cursor, prevLine, prevChar, t, token);
    emitted = t.end;
  }
  return data;
}

bool AbstractSemanticTokenProvider::answersDelta() const {
  const lsp::SemanticTokensOptions options = semanticTokensOptions();
  if (!options.full.has_value()) return false;
  const auto* const full =
      std::get_if<lsp::SemanticTokensFullDelta>(&*options.full);
  return full != nullptr && full->delta.value_or(false);
}

std::optional<lsp::SemanticTokensDelta> AbstractSemanticTokenProvider::keep(
    const Document& document, const std::vector<lsp::Uint>& data,
    std::string& id, const CancellationToken& token,
    std::string_view previousId) const {
  id = std::to_string(++last_result_id);
  // The answers are shared: the comparison and the copies run outside the lock.
  std::shared_ptr<const std::vector<lsp::Uint>> base;
  {
    const std::scoped_lock lock(_mutex);
    // A closed document's colours are asked again in full once it reopens, so
    // its last answer is worth nothing.
    if (document.language != nullptr) {
      const Documents& documents = *document.language->shared().documents;
      std::erase_if(_answers, [&](const auto& entry) {
        if (entry.first == document.uri) return false;
        const std::shared_ptr<Document> held =
            documents.getDocument(entry.first);
        return held == nullptr || !held->isOpen;
      });
    }
    if (const auto held = _answers.find(document.uri);
        held != _answers.end() && !previousId.empty() &&
        held->second.id == previousId)
      base = held->second.data;
  }
  std::optional<lsp::SemanticTokensDelta> delta;
  if (base != nullptr) delta = edits_between(*base, data);
  // A cancelled request keeps nothing: the client's answer stays its base.
  throw_if_cancelled(token);
  // Only an open document's answer is kept; the one it replaces goes after the
  // lock.
  std::shared_ptr<const std::vector<lsp::Uint>> kept;
  if (document.isOpen)
    kept = std::make_shared<const std::vector<lsp::Uint>>(data);
  const std::scoped_lock lock(_mutex);
  if (document.isOpen) {
    Answer& answer = _answers[document.uri];
    answer.id = id;
    std::swap(answer.data, kept);
  } else if (const auto held = _answers.find(document.uri);
             held != _answers.end()) {
    kept = std::move(held->second.data);
    _answers.erase(held);
  }
  return delta;
}

}  // namespace cursiv
