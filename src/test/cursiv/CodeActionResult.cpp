#include "cursiv/CodeActionResult.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/TextDocumentParams.hpp"
#include "cursiv/ValidationHelper.hpp"
#include "cursiv/actions/CodeActionProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/actions/JsonBridge.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

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

  result.document = before.document;
  result.diagnosticsAll = std::move(before.diagnostics);
  const auto matches = [diagnosticCode](const Diagnostic& d) {
    return (d.code && *d.code == diagnosticCode) ||
           dataCode(d) == diagnosticCode;
  };
  const auto count = std::ranges::count_if(result.diagnosticsAll, matches);
  expectedFunction(std::to_string(count), "1");
  if (!count) return result;
  result.diagnosticRelevant =
      *std::ranges::find_if(result.diagnosticsAll, matches);

  const TextDocument& text = before.document->textDocument();
  lsp::CodeActionParams params;
  params.textDocument = textDocumentParams(*before.document);
  params.context.diagnostics.push_back(cursiv::detail::to_lsp_diagnostic(
      *before.document, result.diagnosticRelevant,
      *services.shared().documents));
  params.range = params.context.diagnostics.front().range;
  params.context.triggerKind = lsp::CodeActionTriggerKind::Invoked;
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

  result.action = std::move(actions.front());
  const lsp::CodeAction& action = *result.action;
  // Only the edits filed under this document change its text; an action may
  // edit other documents as well.
  std::vector<TextEdit> converted;
  if (action.edit.has_value())
    for (const lsp::TextEdit& edit : detail::editsFor(*action.edit, text.uri()))
      converted.emplace_back(fromLsp(edit.range), edit.newText);
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
      std::to_string(std::ranges::count_if(after.diagnostics, matches)), "0");
  result.dispose();
  ValidationResult again = validationHelper(services, std::move(input), asked);
  static_cast<Disposable&>(result) = std::move(static_cast<Disposable&>(again));
  result.document = again.document;
  return result;
}

}  // namespace cursiv::test
