#include <pegium/lsp/ranges/DefaultFoldingRangeProvider.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>

#include <algorithm>
#include <utility>

#include <pegium/core/utils/TransparentStringHash.hpp>

namespace pegium {
using namespace pegium::provider_detail;

std::vector<::lsp::FoldingRange>
DefaultFoldingRangeProvider::getFoldingRanges(
    const workspace::Document &document,
    const ::lsp::FoldingRangeParams &params,
    const utils::CancellationToken &cancelToken) const {
  (void)params;
  utils::throw_if_cancelled(cancelToken);
  std::vector<FoldingRangeData> rawRanges;
  if (document.parseResult.cst == nullptr) {
    return {};
  }
  const auto &textDocument = document.textDocument();

  utils::TransparentStringSet seen;
  for (const auto &child : *document.parseResult.cst) {
    collect_folding_ranges(child, textDocument.getText(), rawRanges, seen);
  }

  std::ranges::sort(rawRanges, [](const FoldingRangeData &left,
                                  const FoldingRangeData &right) {
    return left.begin < right.begin;
  });

  const auto &text = textDocument.getText();
  std::vector<::lsp::FoldingRange> ranges;
  ranges.reserve(rawRanges.size());
  for (const auto &range : rawRanges) {
    const auto start = textDocument.positionAt(range.begin);
    const auto end = textDocument.positionAt(range.end);
    if (end.line <= start.line) {
      continue;
    }

    auto endLine = end.line;
    auto endCharacter = end.character;
    if (const char lastCharacter =
            range.end > 0 && range.end <= text.size() ? text[range.end - 1] : '\0';
        !includeLastFoldingLine(range.kind, lastCharacter)) {
      // Keep the closing line visible by ending the range at the end of the
      // previous line.
      const auto lineStart = textDocument.offsetAt(
          ::lsp::Position{.line = end.line, .character = 0});
      if (lineStart == 0) {
        continue;
      }
      const auto trimmed = textDocument.positionAt(lineStart - 1);
      if (trimmed.line <= start.line) {
        continue;
      }
      endLine = trimmed.line;
      endCharacter = trimmed.character;
    }

    ::lsp::FoldingRange folding{};
    folding.startLine = start.line;
    folding.endLine = endLine;
    folding.startCharacter = start.character;
    folding.endCharacter = endCharacter;
    folding.kind = range.kind;
    ranges.push_back(std::move(folding));
  }
  return ranges;
}

bool DefaultFoldingRangeProvider::includeLastFoldingLine(
    const ::lsp::FoldingRangeKindEnum &kind, char lastCharacter) const {
  if (kind == ::lsp::FoldingRangeKind::Comment) {
    return false;
  }
  return lastCharacter != '}' && lastCharacter != ')' && lastCharacter != ']';
}

} // namespace pegium
