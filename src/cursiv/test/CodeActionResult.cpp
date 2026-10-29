#include "cursiv/test/CodeActionResult.hpp"

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/actions/CodeActionProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/TextDocumentParams.hpp"
#include "cursiv/test/ValidationHelper.hpp"

namespace cursiv::test {

namespace {

std::vector<Diagnostic> withCode(const std::vector<Diagnostic>& diagnostics,
                                 std::string_view code) {
  std::vector<Diagnostic> kept;
  for (const Diagnostic& d : diagnostics)
    if (d.code.has_value() && *d.code == code) kept.push_back(d);
  return kept;
}

}  // namespace

CodeActionResult testCodeAction(const LspServices& services, std::string input,
                                std::string_view diagnosticCode,
                                std::optional<std::string_view> outputAfterFix,
                                const ParseHelperOptions& options) {
  ValidationHelperOptions asked;
  static_cast<ParseHelperOptions&>(asked) = options;
  ValidationResult before = validationHelper(services, std::move(input), asked);

  const std::vector<Diagnostic> relevant =
      withCode(before.diagnostics, diagnosticCode);
  expectedFunction(std::to_string(relevant.size()), "1");

  CodeActionResult result;
  result.document = before.document;
  result.diagnosticsAll = before.diagnostics;
  if (relevant.empty()) {
    static_cast<Disposable&>(result) =
        std::move(static_cast<Disposable&>(before));
    return result;
  }
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

  std::optional<ValidationResult> after;
  if (outputAfterFix.has_value()) {
    expectedFunction(std::to_string(actions.size()), "1");
    if (!actions.empty()) {
      result.action = actions.front();
      const lsp::CodeAction& action = *result.action;
      std::vector<TextEdit> converted;
      if (action.edit.has_value() && action.edit->changes.has_value())
        for (const auto& [uri, edits] : *action.edit->changes)
          for (const lsp::TextEdit& edit : edits)
            converted.emplace_back(from_lsp(edit.range), edit.newText);
      expectedFunction(converted.empty() ? "false" : "true", "true",
                       "the code action carries no edit");
      const std::string updated = TextDocument::applyEdits(text, converted);
      expectedFunction(updated, std::string(*outputAfterFix));

      after = validationHelper(services, updated, asked);
      expectedFunction(
          std::to_string(withCode(after->diagnostics, diagnosticCode).size()),
          "0");
    }
  } else {
    expectedFunction(std::to_string(actions.size()), "0");
  }

  Disposable second;
  if (after.has_value()) second = std::move(static_cast<Disposable&>(*after));
  static_cast<Disposable&>(result) = Disposable::create(
      [first = std::move(before), second = std::move(second)]() mutable {
        first.dispose();
        second.dispose();
      });
  return result;
}

}  // namespace cursiv::test
