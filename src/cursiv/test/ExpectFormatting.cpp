#include "cursiv/test/ExpectFormatting.hpp"

#include <lsp/types.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/formatting/Formatter.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/DocumentDisposable.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectationError.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "cursiv/test/TextDocumentParams.hpp"

namespace cursiv::test {

namespace {

// The comparison is about shape, not about which terminator the platform
// writes.
std::string normalizeEOL(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size(); ++at) {
    if (text[at] == '\r' && at + 1 < text.size() && text[at + 1] == '\n')
      continue;
    out.push_back(text[at]);
  }
  return out;
}

lsp::FormattingOptions defaultOptions() {
  lsp::FormattingOptions chosen;
  chosen.insertSpaces = true;
  chosen.tabSize = 4;
  return chosen;
}

}  // namespace

Disposable expectFormatting(const LspServices& services,
                            const ExpectFormatting& expectedFormatting) {
  if (services.formatter == nullptr)
    throw ExpectationError("cursiv: no formatter registered for language " +
                           services.languageMetaData.languageId);

  const std::shared_ptr<Document> document = parseDocument(
      services, expectedFormatting.before, expectedFormatting.parseOptions);
  const lsp::FormattingOptions options =
      expectedFormatting.options.value_or(defaultOptions());

  std::vector<lsp::TextEdit> edits;
  if (expectedFormatting.range.has_value()) {
    lsp::DocumentRangeFormattingParams params;
    params.textDocument = textDocumentParams(*document);
    params.options = options;
    params.range = *expectedFormatting.range;
    edits =
        services.formatter->formatDocumentRange(*document, params, kNoCancel);
  } else {
    lsp::DocumentFormattingParams params;
    params.textDocument = textDocumentParams(*document);
    params.options = options;
    edits = services.formatter->formatDocument(*document, params, kNoCancel);
  }

  std::vector<TextEdit> converted;
  converted.reserve(edits.size());
  for (const lsp::TextEdit& edit : edits)
    converted.push_back({from_lsp(edit.range), edit.newText});
  const std::string edited =
      TextDocument::applyEdits(document->textDocument(), converted);

  expectedFunction(normalizeEOL(edited),
                   normalizeEOL(expectedFormatting.after));

  Disposable disposable = documentDisposable(services, document);
  if (expectedFormatting.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
