#include "cursiv/test/ExpectedRename.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/navigation/RenameProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/DocumentDisposable.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectedBase.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "cursiv/test/RangeUtils.hpp"
#include "cursiv/test/ReplacedIndices.hpp"

namespace cursiv::test {

namespace {

// The edits filed under one URI. The protocol groups by document and says
// nothing about order, so they are read back sorted by position -- which is
// the order the markers are in.
std::vector<lsp::TextEdit> editsFor(const lsp::WorkspaceEdit& all,
                                    std::string_view uri) {
  std::vector<lsp::TextEdit> edits;
  if (!all.changes.has_value()) return edits;
  for (const auto& [key, filed] : *all.changes)
    if (key.toString() == uri) edits = filed;
  std::ranges::sort(edits, [](const lsp::TextEdit& a, const lsp::TextEdit& b) {
    return a.range.start.line == b.range.start.line
               ? a.range.start.character < b.range.start.character
               : a.range.start.line < b.range.start.line;
  });
  return edits;
}

}  // namespace

Disposable expectRename(const LspServices& services,
                        const ExpectedRename& input) {
  const ReplacedIndices marked =
      replaceIndices({.text = input.text,
                      .indexMarker = input.indexMarker,
                      .rangeStartMarker = input.rangeStartMarker,
                      .rangeEndMarker = input.rangeEndMarker});
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, input.parseOptions);
  const lsp::Position caret =
      to_lsp(document->textDocument().positionAt(marked.indices[input.index]));

  std::optional<lsp::Range> box;
  std::optional<lsp::WorkspaceEdit> edit;
  if (services.renameProvider != nullptr) {
    box = services.renameProvider->prepareRename(*document, caret, kNoCancel);
    edit = services.renameProvider->rename(*document, caret, input.newName,
                                           kNoCancel);
  }

  const bool boxExpected = !input.nothingUnderCaret;
  expectedFunction(box.has_value() ? "renamable" : "not renamable",
                   boxExpected ? "renamable" : "not renamable",
                   boxExpected ? "expected an edit box at the caret"
                               : "expected no edit box at the caret");

  if (input.nothingUnderCaret || input.nameRefused) {
    expectedFunction(edit.has_value() ? "edited" : "refused", "refused",
                     "expected the rename to be refused");
  } else {
    expectedFunction(edit.has_value() ? "edited" : "refused", "edited",
                     "expected the rename to file an edit");
    const std::vector<lsp::TextEdit> edits =
        edit.has_value() ? editsFor(*edit, document->textDocument().uri())
                         : std::vector<lsp::TextEdit>{};
    expectedFunction(std::to_string(edits.size()),
                     std::to_string(marked.ranges.size()),
                     "expected " + std::to_string(marked.ranges.size()) +
                         " edits but received " + std::to_string(edits.size()));
    for (std::size_t at = 0; at < marked.ranges.size() && at < edits.size();
         ++at) {
      expectSameRange(edits[at].range,
                      markedRange(*document, marked.ranges[at]));
      expectedFunction(edits[at].newText, input.newName,
                       "expected the edit to write '" + input.newName +
                           "' but it writes '" + edits[at].newText + "'");
    }
  }

  Disposable disposable = documentDisposable(services, document);
  if (input.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
