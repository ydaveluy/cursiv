#include <pegium/lsp/code-actions/DefaultCodeActionProvider.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

#include <pegium/core/validation/DefaultCodeActionProtocol.hpp>
#include <pegium/lsp/support/JsonValue.hpp>

namespace pegium {
namespace {

bool is_quick_fix_kind(const ::lsp::CodeActionKindEnum &kind) {
  return kind == ::lsp::CodeActionKind::QuickFix;
}

bool accepts_quick_fix_only(const ::lsp::CodeActionContext &context) {
  if (!context.only.has_value() || context.only->empty()) {
    return true;
  }
  return std::ranges::any_of(*context.only, is_quick_fix_kind);
}

std::optional<std::string> get_string_field(
    const pegium::JsonValue::Object &object, std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end() || !it->second.isString()) {
    return std::nullopt;
  }
  return it->second.string();
}

std::optional<std::uint32_t> get_uint_field(
    const pegium::JsonValue::Object &object, std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end() || !it->second.isInteger()) {
    return std::nullopt;
  }
  const auto value = it->second.integer();
  if (value < 0) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

} // namespace

std::optional<std::vector<::lsp::OneOf<::lsp::Command, ::lsp::CodeAction>>>
DefaultCodeActionProvider::getCodeActions(
    const workspace::Document &document, const ::lsp::CodeActionParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);

  CodeActionResult actions;
  // The built-in diagnostic quick fixes only apply when the request accepts the
  // QuickFix kind; override-contributed actions decide their own kinds below.
  if (accepts_quick_fix_only(params.context)) {
    for (const auto &diagnostic : params.context.diagnostics) {
      utils::throw_if_cancelled(cancelToken);

      const auto edits = extractDefaultCodeActions(diagnostic);
      if (!edits.has_value()) {
        continue;
      }
      for (const auto &edit : *edits) {
        if (auto action = makeDefaultCodeAction(document, diagnostic, edit);
            action.has_value()) {
          actions.emplace_back(std::move(*action));
        }
      }
    }
  }

  appendCodeActions(document, params, actions, cancelToken);
  if (actions.empty()) {
    return std::nullopt;
  }
  return std::optional<
      std::vector<::lsp::OneOf<::lsp::Command, ::lsp::CodeAction>>>(
      std::move(actions));
}

void DefaultCodeActionProvider::appendCodeActions(
    const workspace::Document &, const ::lsp::CodeActionParams &,
    CodeActionResult &, const utils::CancellationToken &) const {
  /* default: contributes no extra code actions */
}

std::optional<std::vector<DefaultCodeActionProvider::DefaultCodeActionEdit>>
DefaultCodeActionProvider::extractDefaultCodeActions(
    const ::lsp::Diagnostic &diagnostic) {
  if (!diagnostic.data.has_value()) {
    return std::nullopt;
  }

  const auto data = pegium::from_lsp_any(*diagnostic.data);
  if (!data.isObject()) {
    return std::nullopt;
  }

  const auto keyIt =
      data.object().find(std::string(validation::kDefaultCodeActionsKey));
  if (keyIt == data.object().end() || !keyIt->second.isArray()) {
    return std::nullopt;
  }

  std::vector<DefaultCodeActionEdit> actions;
  actions.reserve(keyIt->second.array().size());
  for (const auto &entry : keyIt->second.array()) {
    if (!entry.isObject()) {
      continue;
    }
    const auto &object = entry.object();
    auto kind = get_string_field(object, validation::kCodeActionKindField);
    auto title = get_string_field(object, validation::kCodeActionTitleField);
    auto newText =
        get_string_field(object, validation::kCodeActionNewTextField);
    auto begin = get_uint_field(object, validation::kCodeActionBeginField);
    auto end = get_uint_field(object, validation::kCodeActionEndField);
    // makeDefaultCodeAction derives its result solely from
    // kind/title/newText/begin/end; the editKind field carried in the data
    // object is not consumed here.
    if (!kind.has_value() || !title.has_value() || !newText.has_value() ||
        !begin.has_value() || !end.has_value()) {
      continue;
    }
    actions.push_back({.kind = std::move(*kind),
                       .title = std::move(*title),
                       .newText = std::move(*newText),
                       .begin = *begin,
                       .end = *end});
  }

  return actions.empty() ? std::nullopt
                         : std::optional<std::vector<DefaultCodeActionEdit>>(
                               std::move(actions));
}

std::optional<::lsp::CodeAction> DefaultCodeActionProvider::makeDefaultCodeAction(
    const workspace::Document &document, const ::lsp::Diagnostic &diagnostic,
    const DefaultCodeActionEdit &edit) {
  const auto &textDocument = document.textDocument();
  if (const auto textSize = static_cast<std::uint32_t>(textDocument.getText().size());
      edit.kind != validation::kQuickFixActionKind || edit.begin > edit.end ||
      edit.end > textSize) {
    return std::nullopt;
  }

  ::lsp::CodeAction action{};
  action.title = edit.title;
  action.kind = ::lsp::CodeActionKind::QuickFix;
  action.isPreferred = true;
  action.diagnostics = ::lsp::Array<::lsp::Diagnostic>{diagnostic};

  ::lsp::WorkspaceEdit workspaceEdit{};
  ::lsp::Map<::lsp::DocumentUri, ::lsp::Array<::lsp::TextEdit>> changes;
  ::lsp::TextEdit textEdit{};
  textEdit.range.start = textDocument.positionAt(edit.begin);
  textEdit.range.end = textDocument.positionAt(edit.end);
  textEdit.newText = edit.newText;
  auto &documentChanges = changes[::lsp::Uri::parse(document.uri)];
  documentChanges.push_back(std::move(textEdit));
  workspaceEdit.changes = std::move(changes);
  action.edit = std::move(workspaceEdit);
  return action;
}

} // namespace pegium
