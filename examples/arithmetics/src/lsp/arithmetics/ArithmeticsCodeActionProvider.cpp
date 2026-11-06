#include "arithmetics/ArithmeticsCodeActionProvider.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "arithmetics/Interpreter.hpp"
#include "arithmetics/validation/ArithmeticsValidator.hpp"
#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/services/JsonValue.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace arith {

// The document's own findings, not the request's: what the check attached
// is a `cursiv::JsonValue`, and it survives here untouched. The request's
// copies have crossed the wire and carry the protocol's JSON instead.
void ArithmeticsCodeActionProvider::appendCodeActions(
    const cursiv::Document& document, const lsp::CodeActionParams& params,
    std::vector<lsp::CodeAction>& actions,
    const cursiv::CancellationToken& cancelToken) const {
  const cursiv::TextDocument& text = document.textDocument();
  const cursiv::TextOffset from =
      text.offsetAt(cursiv::from_lsp(params.range.start));
  const cursiv::TextOffset to =
      text.offsetAt(cursiv::from_lsp(params.range.end));

  for (const cursiv::Diagnostic& diagnostic : document.diagnostics) {
    cursiv::throw_if_cancelled(cancelToken);
    if (!diagnostic.code.has_value() || !diagnostic.code->isString()
        || diagnostic.code->string() != kExpressionNormalizable)
      continue;
    if (diagnostic.range.begin > to || diagnostic.range.end < from) continue;
    if (!diagnostic.data.has_value() || !diagnostic.data->isObject()) continue;

    const cursiv::JsonValue::Object& payload = diagnostic.data->object();
    const auto constant = payload.find("constant");
    if (constant == payload.end() || !constant->second.isNumber()) continue;
    // Only a text the grammar reads back: a NUMBER has neither a sign nor
    // an exponent, so `1 - 2` is told it is -1 but offered no rewrite.
    const std::string written = number_text(constant->second.number());
    if (written.empty() || !std::ranges::all_of(written, [](char c) {
          return (c >= '0' && c <= '9') || c == '.';
        }))
      continue;

    lsp::TextEdit edit;
    edit.range = cursiv::to_lsp(
        text.rangeAt(diagnostic.range.begin, diagnostic.range.end));
    edit.newText = written;

    lsp::WorkspaceEdit workspaceEdit;
    lsp::Map<lsp::DocumentUri, lsp::Array<lsp::TextEdit>> changes;
    changes[lsp::Uri::parse(std::string(document.uri))].push_back(
        std::move(edit));
    workspaceEdit.changes = std::move(changes);

    // The squiggle the fix answers, so the lightbulb sits on it.
    lsp::Diagnostic about;
    about.range = edit.range;
    about.message = diagnostic.message;

    lsp::CodeAction action;
    action.title = std::format("Replace with constant {}", written);
    action.kind = lsp::CodeActionKind::QuickFix;
    action.diagnostics = lsp::Array<lsp::Diagnostic>{std::move(about)};
    action.isPreferred = true;
    action.edit = std::move(workspaceEdit);
    actions.push_back(std::move(action));
  }
}

}  // namespace arith
