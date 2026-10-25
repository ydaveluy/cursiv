#include "cursiv/lsp/actions/DefaultCodeActionProvider.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/core/validation/SpellingActions.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/actions/JsonBridge.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "lsp/types.h"
#include "lsp/uri.h"

namespace cursiv {
namespace {

// The request's `only` filter: absent or empty accepts everything; otherwise
// the QuickFix kind must be among what the client asked for.
[[nodiscard]] bool accepts_quick_fixes(const lsp::CodeActionContext& context) {
  if (!context.only.has_value() || context.only->empty()) return true;
  return std::ranges::any_of(*context.only,
                             [](const lsp::CodeActionKindEnum& kind) {
                               return kind == lsp::CodeActionKind::QuickFix;
                             });
}

[[nodiscard]] const JsonValue* field(const JsonValue::Object& object,
                                     std::string_view key) {
  const auto it = object.find(key);
  return it == object.end() ? nullptr : &it->second;
}

[[nodiscard]] bool same_json_payload(const lsp::LSPAny& expected,
                                     const lsp::LSPAny& actual) {
  // A JSON round-trip can turn an integral decimal into an integer.
  if (expected.isNumber() && actual.isNumber())
    return expected.number() == actual.number();
  if (expected.isArray() && actual.isArray())
    return std::ranges::equal(expected.array(), actual.array(),
                              same_json_payload);
  if (expected.isObject() && actual.isObject()) {
    if (expected.object().size() != actual.object().size()) return false;
    // The server's keys are unique; client objects can contain duplicates.
    for (const auto& [key, value] : expected.object()) {
      const auto* other = actual.object().find(key);
      if (other == nullptr || !same_json_payload(value, *other)) return false;
    }
    return true;
  }
  return expected == actual;
}

[[nodiscard]] bool matches_diagnostic(const Diagnostic& diagnostic,
                                      const lsp::Range& range,
                                      const lsp::Diagnostic& candidate) {
  const auto* message = std::get_if<lsp::String>(&candidate.message);
  if (message == nullptr || *message != diagnostic.message ||
      from_lsp(candidate.range) != from_lsp(range) ||
      !candidate.severity.has_value() ||
      candidate.severity->value() !=
          static_cast<lsp::Uint>(diagnostic.severity) ||
      candidate.source.value_or("") != diagnostic.source ||
      candidate.code.has_value() != diagnostic.code.has_value() ||
      candidate.data.has_value() != diagnostic.data.has_value())
    return false;
  if (diagnostic.code.has_value()) {
    if (diagnostic.code->isString()) {
      const auto* code = std::get_if<lsp::String>(&*candidate.code);
      if (code == nullptr || *code != diagnostic.code->string()) return false;
    } else {
      const auto* code = std::get_if<lsp::Int>(&*candidate.code);
      if (code == nullptr ||
          *code != static_cast<int>(diagnostic.code->integer()))
        return false;
    }
  }
  return !diagnostic.data.has_value() ||
         same_json_payload(to_lsp_any(*diagnostic.data), *candidate.data);
}

// One edit row into one code action; null when the row is malformed, stale
// (another revision's offsets) or out of bounds -- a dropped fix reappears
// with the next build's fresh block.
[[nodiscard]] std::optional<lsp::CodeAction> make_action(
    const Document& document, const Diagnostic& diagnostic,
    const JsonValue& row, std::span<const lsp::Diagnostic> clientDiagnostics) {
  if (!row.isObject()) return std::nullopt;
  const JsonValue::Object& object = row.object();
  // The edit is derived from kind/title/newText/begin/end/revision alone: the
  // `editKind` the writer also carries is not consumed here -- insert against
  // replace is what `begin == end` already says.
  const JsonValue* kind = field(object, kKindField);
  const JsonValue* title = field(object, kTitleField);
  const JsonValue* newText = field(object, kNewTextField);
  const JsonValue* begin = field(object, kBeginField);
  const JsonValue* end = field(object, kEndField);
  const JsonValue* revision = field(object, kRevisionField);
  if (kind == nullptr || !kind->isString() || kind->string() != kQuickFixKind ||
      title == nullptr || !title->isString() || newText == nullptr ||
      !newText->isString() || begin == nullptr || !begin->isInteger() ||
      end == nullptr || !end->isInteger() || revision == nullptr ||
      !revision->isInteger())
    return std::nullopt;
  if (revision->integer() != static_cast<std::int64_t>(document.buildRevision))
    return std::nullopt;
  const std::int64_t editBegin = begin->integer();
  const std::int64_t editEnd = end->integer();
  if (editBegin < 0 || editEnd < editBegin ||
      static_cast<std::size_t>(editEnd) >
          document.textDocument().getText().size())
    return std::nullopt;

  lsp::CodeAction action;
  action.title = title->string();
  action.kind = lsp::CodeActionKind::QuickFix;
  action.isPreferred = true;

  // The finding this action answers, in the client's shape, so the editor
  // groups the lightbulb with the squiggle.
  lsp::Diagnostic about;
  about.range = to_lsp(document.textDocument().rangeAt(diagnostic.range.begin,
                                                       diagnostic.range.end));
  about.message = diagnostic.message;
  for (const lsp::Diagnostic& candidate : clientDiagnostics)
    if (matches_diagnostic(diagnostic, about.range, candidate)) {
      about = candidate;
      break;
    }
  action.diagnostics = lsp::Array<lsp::Diagnostic>{std::move(about)};

  lsp::TextEdit edit;
  edit.range = to_lsp(document.textDocument().rangeAt(
      static_cast<TextOffset>(editBegin), static_cast<TextOffset>(editEnd)));
  edit.newText = newText->string();
  lsp::WorkspaceEdit workspaceEdit;
  lsp::Map<lsp::DocumentUri, lsp::Array<lsp::TextEdit>> changes;
  changes[lsp::Uri::parse(std::string(document.uri))].push_back(
      std::move(edit));
  workspaceEdit.changes = std::move(changes);
  action.edit = std::move(workspaceEdit);
  return action;
}

}  // namespace

std::vector<lsp::CodeAction> DefaultCodeActionProvider::getCodeActions(
    const Document& document, const lsp::CodeActionParams& params,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  std::vector<lsp::CodeAction> actions;
  // The `only` filter gates the built-in quick fixes alone: the hook below
  // decides its own kinds.
  if (accepts_quick_fixes(params.context)) {
    // The request's range, as offsets over this text -- what selects which
    // findings the client is asking about. Touching counts: a caret sitting
    // on a zero-width repair point must reach it.
    const TextOffset from =
        document.textDocument().offsetAt(from_lsp(params.range.start));
    const TextOffset to =
        document.textDocument().offsetAt(from_lsp(params.range.end));

    const auto offer = [&](const Diagnostic& diagnostic, const JsonValue& row) {
      if (std::optional<lsp::CodeAction> action = make_action(
              document, diagnostic, row, params.context.diagnostics);
          action.has_value())
        actions.push_back(std::move(*action));
    };
    std::vector<const Diagnostic*> asked;
    for (const Diagnostic& diagnostic : document.diagnostics)
      if (diagnostic.range.begin <= to && diagnostic.range.end >= from)
        asked.push_back(&diagnostic);
    // Asked here, for the findings under the request: the lookup walks the
    // reference's whole scope, which no validation pays for every site.
    throw_if_cancelled(token);
    const std::vector<JsonValue::Array> spelled =
        spelling_actions(document, asked);
    for (std::size_t i = 0; i < asked.size(); ++i) {
      throw_if_cancelled(token);
      const Diagnostic& diagnostic = *asked[i];
      if (diagnostic.data.has_value() && diagnostic.data->isObject())
        if (const JsonValue* rows =
                field(diagnostic.data->object(), kDefaultCodeActionsKey);
            rows != nullptr && rows->isArray())
          for (const JsonValue& row : rows->array()) offer(diagnostic, row);
      for (const JsonValue& row : spelled[i]) offer(diagnostic, row);
    }
  }
  appendCodeActions(document, params, actions, token);
  return actions;
}

void DefaultCodeActionProvider::appendCodeActions(
    const Document&, const lsp::CodeActionParams&,
    std::vector<lsp::CodeAction>&, const CancellationToken&) const {}

}  // namespace cursiv
