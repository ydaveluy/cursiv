#include "cursiv/CodeActionResult.hpp"

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/TextDocumentParams.hpp"
#include "cursiv/ValidationHelper.hpp"
#include "cursiv/actions/CodeActionProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

namespace {

std::vector<Diagnostic> withCode(const std::vector<Diagnostic>& diagnostics,
                                 std::string_view code) {
  std::vector<Diagnostic> kept;
  for (const Diagnostic& d : diagnostics)
    if ((d.code.has_value() && *d.code == code) || data_code(d) == code)
      kept.push_back(d);
  return kept;
}

}  // namespace

CodeActionResult testCodeAction(const LspServices& services, std::string input,
                                std::string_view diagnosticCode,
                                std::optional<std::string_view> outputAfterFix,
                                const ParseHelperOptions& options) {
  ValidationHelperOptions asked;
  static_cast<ParseHelperOptions&>(asked) = options;
  ValidationResult before = validationHelper(services, input, asked);

  // One cleanup, for whichever text stands in the workspace.
  CodeActionResult result;
  static_cast<Disposable&>(result) =
      std::move(static_cast<Disposable&>(before));
  const detail::DisposeOnThrow cleanup(result);

  const std::vector<Diagnostic> relevant =
      withCode(before.diagnostics, diagnosticCode);
  expectedFunction(std::to_string(relevant.size()), "1");

  result.document = before.document;
  result.diagnosticsAll = before.diagnostics;
  if (relevant.empty()) return result;
  result.diagnosticRelevant = relevant.front();

  const TextDocument& text = before.document->textDocument();
  lsp::CodeActionParams params;
  params.textDocument = textDocumentParams(*before.document);
  params.range = to_lsp(text.rangeAt(result.diagnosticRelevant.range.begin,
                                     result.diagnosticRelevant.range.end));
  // The context stays empty: this tree's provider reads the document's own
  // published block rather than the client's echo of it.
  std::vector<lsp::CodeAction> actions;
  if (services.codeActionProvider != nullptr)
    actions = services.codeActionProvider->getCodeActions(*before.document,
                                                          params, kNoCancel);

  if (!outputAfterFix.has_value()) {
    expectedFunction(std::to_string(actions.size()), "0");
    return result;
  }
  expectedFunction(std::to_string(actions.size()), "1");
  if (actions.empty()) return result;

  result.action = actions.front();
  const lsp::CodeAction& action = *result.action;
  // Only the edits filed under this document change its text; an action may
  // edit other documents as well.
  std::vector<TextEdit> converted;
  if (action.edit.has_value() && action.edit->changes.has_value())
    for (const auto& [uri, edits] : *action.edit->changes)
      if (uri.toString() == text.uri())
        for (const lsp::TextEdit& edit : edits)
          converted.emplace_back(from_lsp(edit.range), edit.newText);
  expectedFunction(converted.empty() ? "false" : "true", "true",
                   "the code action carries no edit for this document");
  const std::string updated = TextDocument::applyEdits(text, converted);
  expectedFunction(updated, *outputAfterFix);

  // The edited text is checked under the input's URI, as an editor applies
  // the action; then the input is built again for the caller.
  asked.documentUri = before.document->uri;
  result.dispose();
  ValidationResult after = validationHelper(services, updated, asked);
  static_cast<Disposable&>(result) = std::move(static_cast<Disposable&>(after));
  expectedFunction(
      std::to_string(withCode(after.diagnostics, diagnosticCode).size()), "0");
  result.dispose();
  ValidationResult again = validationHelper(services, std::move(input), asked);
  static_cast<Disposable&>(result) = std::move(static_cast<Disposable&>(again));
  result.document = again.document;
  return result;
}

}  // namespace cursiv::test
