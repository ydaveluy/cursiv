#include <pegium/lsp/semantic/AbstractSemanticTokenProvider.hpp>
#include <pegium/lsp/workspace/TextDocuments.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/syntax-tree/CstUtils.hpp>
#include <pegium/core/text/Utf8Utf16.hpp>

#include <cassert>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace pegium {

namespace {

struct EncodedSemanticToken {
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  std::uint32_t length = 0;
  std::uint32_t type = 0;
  std::uint32_t modifiers = 0;
};

constexpr std::array<std::string_view, 23> kSemanticTokenTypes = {
    "class",         "comment",   "enum",        "enumMember",
    "event",         "function",  "interface",   "keyword",
    "macro",         "method",    "modifier",    "namespace",
    "number",        "operator",  "parameter",   "property",
    "regexp",        "string",    "struct",      "type",
    "typeParameter", "variable",  "decorator"};

constexpr std::array<std::string_view, 10> kSemanticTokenModifiers = {
    "abstract",     "async",       "declaration", "defaultLibrary",
    "definition",   "deprecated",  "documentation",
    "modification", "readonly",    "static"};

::pegium::SemanticTokenProvider::StringIndexMap make_type_map() {
  ::pegium::SemanticTokenProvider::StringIndexMap map;
  for (std::uint32_t index = 0; index < kSemanticTokenTypes.size(); ++index) {
    map.try_emplace(std::string(kSemanticTokenTypes[index]), index);
  }
  return map;
}

::pegium::SemanticTokenProvider::StringIndexMap make_modifier_map() {
  ::pegium::SemanticTokenProvider::StringIndexMap map;
  for (std::uint32_t index = 0; index < kSemanticTokenModifiers.size(); ++index) {
    map.try_emplace(std::string(kSemanticTokenModifiers[index]), 1u << index);
  }
  return map;
}

const ::pegium::SemanticTokenProvider::StringIndexMap &semantic_token_types() {
  static const auto map = make_type_map();
  return map;
}

const ::pegium::SemanticTokenProvider::StringIndexMap &
semantic_token_modifiers() {
  static const auto map = make_modifier_map();
  return map;
}

::lsp::Range to_lsp_range(const workspace::Document &document,
                          TextOffset begin, TextOffset end) {
  const auto &textDocument = document.textDocument();
  ::lsp::Range range{};
  range.start = textDocument.positionAt(begin);
  range.end = textDocument.positionAt(end);
  return range;
}

bool ranges_overlap(const workspace::Document &document, const ::lsp::Range &left,
                    const ::lsp::Range &right) {
  const auto &textDocument = document.textDocument();
  const auto leftBegin = textDocument.offsetAt(left.start);
  const auto leftEnd = textDocument.offsetAt(left.end);
  const auto rightBegin = textDocument.offsetAt(right.start);
  const auto rightEnd = textDocument.offsetAt(right.end);
  return leftBegin < rightEnd && rightBegin < leftEnd;
}

std::vector<std::string>
to_string_vector(std::initializer_list<std::string_view> values) {
  std::vector<std::string> result;
  result.reserve(values.size());
  for (const auto value : values) {
    result.emplace_back(value);
  }
  return result;
}

std::uint32_t utf16_length(std::string_view text) {
  std::uint32_t length = 0;
  auto remaining = text;
  while (!remaining.empty()) {
    std::uint32_t advance = 0;
    std::uint32_t utf16Units = 0;
    text::decodeOneUtf8ToUtf16Units(
        reinterpret_cast<const std::byte *>(remaining.data()),
        static_cast<std::uint32_t>(remaining.size()), advance, utf16Units);
    remaining.remove_prefix(std::min<std::size_t>(advance, remaining.size()));
    length += utf16Units;
  }
  return length;
}

std::uint32_t utf16_length_between_offsets(const workspace::Document &document,
                                           TextOffset begin, TextOffset end) {
  if (end <= begin) {
    return 0;
  }
  return utf16_length(document.textDocument().getText().substr(
      static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin)));
}

TextOffset line_content_end_offset(const workspace::Document &document,
                                   std::uint32_t line) {
  const auto &textDocument = document.textDocument();
  auto end = textDocument.offsetAt(line + 1, 0);
  if (end == 0) {
    return 0;
  }

  const auto text = textDocument.getText();
  if (static_cast<std::size_t>(end) <= text.size() &&
      text[static_cast<std::size_t>(end - 1)] == '\n') {
    --end;
  }
  if (end > 0 && static_cast<std::size_t>(end) <= text.size() &&
      text[static_cast<std::size_t>(end - 1)] == '\r') {
    --end;
  }
  return end;
}

void append_encoded_token(std::vector<EncodedSemanticToken> &tokens,
                          std::uint32_t line, std::uint32_t character,
                          std::uint32_t length, std::uint32_t type,
                          std::uint32_t modifiers) {
  if (length == 0) {
    return;
  }

  tokens.push_back({.line = line,
                    .character = character,
                    .length = length,
                    .type = type,
                    .modifiers = modifiers});
}

void append_semantic_token(
    const workspace::Document &document,
    const std::optional<::lsp::Range> &filterRange,
    const SemanticTokenInfo &token,
    bool supportsMultilineTokens,
    std::vector<EncodedSemanticToken> &tokens) {
  if (filterRange.has_value() &&
      !ranges_overlap(document, token.range, *filterRange)) {
    return;
  }

  const auto &typeMap = semantic_token_types();
  const auto typeIt = typeMap.find(token.type);
  if (typeIt == typeMap.end()) {
    return;
  }

  std::uint32_t modifiers = 0;
  const auto &modifierMap = semantic_token_modifiers();
  for (const auto &modifier : token.modifiers) {
    if (const auto it = modifierMap.find(modifier); it != modifierMap.end()) {
      modifiers |= it->second;
    }
  }

  const auto &textDocument = document.textDocument();
  const auto startOffset = textDocument.offsetAt(token.range.start);
  const auto endOffset = textDocument.offsetAt(token.range.end);
  if (endOffset <= startOffset) {
    return;
  }

  const auto startLine = token.range.start.line;
  const auto endLine = token.range.end.line;
  if (startLine == endLine) {
    append_encoded_token(tokens, startLine, token.range.start.character,
                         token.range.end.character - token.range.start.character,
                         typeIt->second, modifiers);
    return;
  }

  if (supportsMultilineTokens) {
    append_encoded_token(tokens, startLine, token.range.start.character,
                         utf16_length_between_offsets(document, startOffset,
                                                      endOffset),
                         typeIt->second, modifiers);
    return;
  }

  auto segmentStart = startOffset;
  auto line = startLine;
  std::uint32_t character = token.range.start.character;
  while (line < endLine) {
    const auto segmentEnd = line_content_end_offset(document, line);
    append_encoded_token(tokens, line, character,
                         utf16_length_between_offsets(document, segmentStart,
                                                      segmentEnd),
                         typeIt->second, modifiers);
    ++line;
    segmentStart = textDocument.offsetAt(line, 0);
    character = 0;
  }

  append_encoded_token(tokens, endLine, 0,
                       utf16_length_between_offsets(document, segmentStart,
                                                    endOffset),
                       typeIt->second, modifiers);
}

} // namespace

AbstractSemanticTokenProvider::AbstractSemanticTokenProvider(
    const pegium::Services &services)
    : DefaultLanguageService(services) {
  auto *languageServer = services.shared.lsp.languageServer.get();
  assert(languageServer != nullptr);
  _languageServerInitializeSubscription =
      languageServer->onInitialize([this](const ::lsp::InitializeParams &params) {
        _supportsMultilineTokens.store(
            params.capabilities.textDocument.has_value() &&
                    params.capabilities.textDocument->semanticTokens.has_value() &&
                    params.capabilities.textDocument->semanticTokens
                        ->multilineTokenSupport.value_or(false)
                ? std::uint8_t{1}
                : std::uint8_t{0});
      });

  const auto documents = services.shared.lsp.textDocuments;
  assert(documents != nullptr);
  _textDocumentCloseSubscription =
      documents->onDidClose([this](const workspace::TextDocumentChangeEvent &event) {
        clearSemanticTokenCache(event.document->uri());
      });
}

AbstractSemanticTokenProvider::StringIndexMap
AbstractSemanticTokenProvider::tokenTypes() const {
  return semantic_token_types();
}

AbstractSemanticTokenProvider::StringIndexMap
AbstractSemanticTokenProvider::tokenModifiers() const {
  return semantic_token_modifiers();
}

::lsp::SemanticTokensOptions
AbstractSemanticTokenProvider::semanticTokensOptions() const {
  ::lsp::SemanticTokensOptions options{};
  for (const auto type : kSemanticTokenTypes) {
    options.legend.tokenTypes.emplace_back(type);
  }
  for (const auto modifier : kSemanticTokenModifiers) {
    options.legend.tokenModifiers.emplace_back(modifier);
  }
  options.range = true;
  ::lsp::SemanticTokensOptionsFull full{};
  // Advertise delta support: semanticHighlightDelta() is fully implemented
  // (result-id tracking + cached-snapshot diffing) and unit-tested, so clients
  // can use the incremental path instead of re-requesting the full token set.
  full.delta = true;
  options.full = full;
  return options;
}

std::optional<::lsp::SemanticTokens>
AbstractSemanticTokenProvider::semanticHighlight(
    const workspace::Document &document, const ::lsp::SemanticTokensParams &params,
    const utils::CancellationToken &cancelToken) const {
  (void)params;
  auto tokens = buildSemanticTokens(document, std::nullopt, cancelToken);
  if (!tokens.has_value()) {
    return std::nullopt;
  }
  tokens->resultId = nextSemanticTokenResultId();
  setSemanticTokenCache(document.uri, *tokens);
  return tokens;
}

std::optional<::lsp::SemanticTokens>
AbstractSemanticTokenProvider::semanticHighlightRange(
    const workspace::Document &document,
    const ::lsp::SemanticTokensRangeParams &params,
    const utils::CancellationToken &cancelToken) const {
  auto tokens = buildSemanticTokens(document, params.range, cancelToken);
  if (!tokens.has_value()) {
    return std::nullopt;
  }
  tokens->resultId = nextSemanticTokenResultId();
  return tokens;
}

std::optional<::lsp::OneOf<::lsp::SemanticTokens, ::lsp::SemanticTokensDelta>>
AbstractSemanticTokenProvider::semanticHighlightDelta(
    const workspace::Document &document,
    const ::lsp::SemanticTokensDeltaParams &params,
    const utils::CancellationToken &cancelToken) const {
  auto full = buildSemanticTokens(document, std::nullopt, cancelToken);
  if (!full.has_value()) {
    return std::nullopt;
  }
  full->resultId = nextSemanticTokenResultId();

  const auto cached = getSemanticTokenCache(document.uri);
  setSemanticTokenCache(document.uri, *full);

  if (!cached.has_value() || cached->resultId != params.previousResultId) {
    return ::lsp::OneOf<::lsp::SemanticTokens, ::lsp::SemanticTokensDelta>{
        std::move(*full)};
  }

  ::lsp::SemanticTokensDelta delta{};
  delta.resultId = *full->resultId;
  if (cached->data != full->data) {
    ::lsp::SemanticTokensEdit edit{};
    edit.start = 0;
    edit.deleteCount = static_cast<std::uint32_t>(cached->data.size());
    edit.data = full->data;
    delta.edits.push_back(std::move(edit));
  }
  return ::lsp::OneOf<::lsp::SemanticTokens, ::lsp::SemanticTokensDelta>{
      std::move(delta)};
}

void AbstractSemanticTokenProvider::highlightRange(
    const ::lsp::Range &range, std::string_view type,
    const SemanticTokenAcceptor &acceptor,
    std::initializer_list<std::string_view> modifiers) const {
  acceptor(SemanticTokenInfo{
      .range = range,
      .type = std::string(type),
      .modifiers = to_string_vector(modifiers),
  });
}

void AbstractSemanticTokenProvider::highlightNode(
    const AstNode &node, std::string_view type,
    const SemanticTokenAcceptor &acceptor,
    std::initializer_list<std::string_view> modifiers) const {
  if (!node.hasCstNode()) {
    return;
  }
  const auto &document = getDocument(node);
  highlightRange(
      to_lsp_range(document, node.getCstNode().getBegin(), node.getCstNode().getEnd()),
      type, acceptor, modifiers);
}

void AbstractSemanticTokenProvider::highlightProperty(
    const AstNode &node, std::string_view feature, std::string_view type,
    const SemanticTokenAcceptor &acceptor,
    std::initializer_list<std::string_view> modifiers,
    std::optional<std::size_t> index) const {
  if (!node.hasCstNode()) {
    return;
  }
  const auto &document = getDocument(node);

  const auto nodes = find_nodes_for_feature(node.getCstNode(), feature);
  if (index.has_value()) {
    if (*index >= nodes.size()) {
      return;
    }
    const auto &cstNode = nodes[*index];
    highlightRange(to_lsp_range(document, cstNode.getBegin(), cstNode.getEnd()),
                   type, acceptor, modifiers);
    return;
  }

  for (const auto &cstNode : nodes) {
    highlightRange(to_lsp_range(document, cstNode.getBegin(), cstNode.getEnd()),
                   type, acceptor, modifiers);
  }
}

void AbstractSemanticTokenProvider::highlightKeyword(
    const AstNode &node, std::string_view keyword, std::string_view type,
    const SemanticTokenAcceptor &acceptor,
    std::initializer_list<std::string_view> modifiers,
    std::optional<std::size_t> index) const {
  if (!node.hasCstNode()) {
    return;
  }
  const auto &document = getDocument(node);

  auto emit = [this, &document, &node, keyword, type, &acceptor,
               modifiers](std::size_t itemIndex) {
    if (auto cstNode = find_node_for_keyword(node.getCstNode(), keyword, itemIndex);
        cstNode.has_value()) {
      highlightRange(
          to_lsp_range(document, cstNode->getBegin(), cstNode->getEnd()), type,
          acceptor, modifiers);
      return true;
    }
    return false;
  };

  if (index.has_value()) {
    (void)emit(*index);
    return;
  }

  for (std::size_t itemIndex = 0; emit(itemIndex); ++itemIndex) {
  }
}

std::optional<::lsp::SemanticTokens>
AbstractSemanticTokenProvider::buildSemanticTokens(
    const workspace::Document &document, const std::optional<::lsp::Range> &range,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  ::lsp::SemanticTokens result{};
  if (!document.hasAst()) {
    return result;
  }

  std::vector<EncodedSemanticToken> tokens;
  const SemanticTokenAcceptor acceptor =
      [&document, &range,
       supportsMultilineTokens = _supportsMultilineTokens.load() != 0,
       &tokens](SemanticTokenInfo token) {
    append_semantic_token(document, range, token,
                          supportsMultilineTokens, tokens);
      };

  const auto &textDocument = document.textDocument();
  TextOffset rangeBegin = 0;
  TextOffset rangeEnd = 0;
  if (range.has_value()) {
    rangeBegin = textDocument.offsetAt(range->start);
    rangeEnd = textDocument.offsetAt(range->end);
  }

  auto visit = [this, &range, rangeBegin, rangeEnd, &cancelToken,
                &acceptor](const auto &self, const AstNode &node) {
    if (!node.hasCstNode()) {
      return;
    }
    if (range.has_value() && (node.getCstNode().getEnd() < rangeBegin ||
                              node.getCstNode().getBegin() > rangeEnd)) {
      return;
    }
    utils::throw_if_cancelled(cancelToken);
    highlightElement(node, acceptor);
    for (const auto *child : node.getContent()) {
      self(self, *child);
    }
  };

  visit(visit, *document.parseResult.value);
  std::ranges::sort(tokens, [](const EncodedSemanticToken &left,
                               const EncodedSemanticToken &right) {
    if (left.line == right.line) {
      return left.character < right.character;
    }
    return left.line < right.line;
  });

  std::uint32_t previousLine = 0;
  std::uint32_t previousCharacter = 0;
  for (const auto &token : tokens) {
    const auto deltaLine = token.line - previousLine;
    const auto deltaCharacter =
        deltaLine == 0 ? token.character - previousCharacter : token.character;
    result.data.push_back(deltaLine);
    result.data.push_back(deltaCharacter);
    result.data.push_back(token.length);
    result.data.push_back(token.type);
    result.data.push_back(token.modifiers);
    previousLine = token.line;
    previousCharacter = token.character;
  }

  return result;
}

std::string AbstractSemanticTokenProvider::nextSemanticTokenResultId() const {
  std::lock_guard lock(_semanticTokenCacheMutex);
  return std::to_string(_nextSemanticTokenResultId++);
}

std::optional<AbstractSemanticTokenProvider::SemanticTokenCacheEntry>
AbstractSemanticTokenProvider::getSemanticTokenCache(std::string_view uri) const {
  std::lock_guard lock(_semanticTokenCacheMutex);
  const auto it = _semanticTokenCache.find(std::string(uri));
  if (it == _semanticTokenCache.end()) {
    return std::nullopt;
  }
  return it->second;
}

void AbstractSemanticTokenProvider::setSemanticTokenCache(
    std::string_view uri, const ::lsp::SemanticTokens &tokens) const {
  if (!tokens.resultId.has_value()) {
    return;
  }

  std::lock_guard lock(_semanticTokenCacheMutex);
  _semanticTokenCache.insert_or_assign(
      std::string(uri),
      SemanticTokenCacheEntry{.data = {tokens.data.begin(), tokens.data.end()},
                              .resultId = *tokens.resultId});
}

void AbstractSemanticTokenProvider::clearSemanticTokenCache(
    std::string_view uri) const {
  std::lock_guard lock(_semanticTokenCacheMutex);
  _semanticTokenCache.erase(std::string(uri));
}

} // namespace pegium
