#include <lsp/messages.h>
#include <lsp/uri.h>

#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/actions/DefaultCodeActionProvider.hpp"
#include "cursiv/lsp/actions/JsonBridge.hpp"
#include "cursiv/test/Check.hpp"

namespace {

struct ObservedActions final : cursiv::DefaultCodeActionProvider {
  mutable bool called = false;

  void appendCodeActions(const cursiv::Document&, const lsp::CodeActionParams&,
                         std::vector<lsp::CodeAction>&,
                         const cursiv::CancellationToken&) const override {
    called = true;
  }
};

void cancellation_precedes_a_filter_that_excludes_quick_fixes() {
  const cursiv::Document document(
      cursiv::TextDocument::create("file:///cancel.ws", "workspace", 1, ""));
  const ObservedActions provider;
  lsp::CodeActionParams params;
  params.context.only = lsp::Array<lsp::CodeActionKindEnum>{
      lsp::CodeActionKindEnum(lsp::CodeActionKind::Source)};
  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool cancelled = false;
  try {
    (void)provider.getCodeActions(document, params, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  CHECK(!provider.called);
  CHECK(provider.getCodeActions(document, params).empty());
  CHECK(provider.called);
}

void an_action_preserves_the_matching_diagnostic_and_rejects_stale_payloads() {
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///diagnostic.ws", "workspace", 1, "a"));
  cursiv::Diagnostic diagnostic;
  diagnostic.range = {0, 1};
  diagnostic.message = "replace a";
  diagnostic.source = "audit";
  diagnostic.severity = cursiv::DiagnosticSeverity::Warning;
  diagnostic.code = cursiv::DiagnosticCode("audit.replace");
  cursiv::JsonValue::Object data;
  data[std::string(cursiv::kDefaultCodeActionsKey)] = cursiv::JsonValue::Array{
      cursiv::make_action_row(cursiv::kReplaceEdit, "replace", {0, 1}, "b", 0)};
  data["numbers"] = cursiv::JsonValue::Array{
      cursiv::JsonValue(1.0), cursiv::JsonValue(cursiv::JsonValue::Object{
                                  {"nested", cursiv::JsonValue(2.0)}})};
  diagnostic.data = cursiv::JsonValue(std::move(data));
  document.diagnostics.push_back(std::move(diagnostic));

  lsp::CodeActionParams params;
  params.range = {{0, 0}, {0, 1}};
  lsp::Diagnostic echo;
  echo.range = params.range;
  echo.message = std::string("replace a");
  echo.severity = lsp::DiagnosticSeverity::Warning;
  echo.source = "audit";
  echo.code = std::string("audit.replace");
  echo.data = cursiv::to_lsp_any(*document.diagnostics.front().data);
  echo.tags =
      lsp::Array<lsp::DiagnosticTagEnum>{lsp::DiagnosticTag::Deprecated};
  lsp::CodeDescription description;
  description.href = lsp::Uri::parse("https://example.test/diagnostic");
  echo.codeDescription = std::move(description);
  lsp::DiagnosticRelatedInformation related;
  related.location.uri = lsp::Uri::parse("file:///related.ws");
  related.location.range = {{2, 3}, {2, 4}};
  related.message = "related";
  echo.relatedInformation =
      lsp::Array<lsp::DiagnosticRelatedInformation>{std::move(related)};
  params.context.diagnostics.push_back(echo);

  const cursiv::DefaultCodeActionProvider provider;
  const auto actions = provider.getCodeActions(document, params);
  CHECK(actions.size() == 1);
  if (actions.size() != 1) return;
  CHECK(actions.front().diagnostics.has_value());
  if (!actions.front().diagnostics.has_value()) return;
  CHECK(actions.front().diagnostics->size() == 1);
  if (actions.front().diagnostics->size() != 1) return;
  const lsp::Diagnostic& about = actions.front().diagnostics->front();
  CHECK(about.severity == lsp::DiagnosticSeverity::Warning);
  CHECK(about.source == echo.source);
  CHECK(about.code == echo.code);
  CHECK(about.data == echo.data);
  CHECK(about.codeDescription.has_value());
  CHECK(about.tags.has_value() && about.tags->size() == 1);
  CHECK(about.relatedInformation.has_value() &&
        about.relatedInformation->size() == 1);
  if (about.relatedInformation.has_value() &&
      !about.relatedInformation->empty())
    CHECK(about.relatedInformation->front().message == "related");

  auto& numbers = params.context.diagnostics.front().data->object()["numbers"];
  numbers.array().front() = lsp::LSPAny(1);
  numbers.array().back().object()["nested"] = lsp::LSPAny(2);
  const auto normalized = provider.getCodeActions(document, params);
  CHECK(normalized.size() == 1);
  if (normalized.size() == 1 && normalized.front().diagnostics.has_value() &&
      !normalized.front().diagnostics->empty()) {
    CHECK(normalized.front().diagnostics->front().data ==
          params.context.diagnostics.front().data);
    CHECK(normalized.front().diagnostics->front().tags.has_value());
  } else {
    CHECK(false);
  }

  numbers.array().front() = lsp::LSPAny(3);
  const auto changed = provider.getCodeActions(document, params);
  CHECK(changed.size() == 1);
  if (changed.size() == 1 && changed.front().diagnostics.has_value() &&
      !changed.front().diagnostics->empty())
    CHECK(!changed.front().diagnostics->front().data.has_value());
  else
    CHECK(false);

  params.context.diagnostics.front() = echo;
  auto& duplicate = params.context.diagnostics.front().data->object();
  duplicate.remove("numbers");
  duplicate.append(std::string(cursiv::kDefaultCodeActionsKey),
                   duplicate.get(cursiv::kDefaultCodeActionsKey));
  const auto duplicated = provider.getCodeActions(document, params);
  CHECK(duplicated.size() == 1);
  if (duplicated.size() == 1 && duplicated.front().diagnostics.has_value() &&
      duplicated.front().diagnostics->size() == 1)
    CHECK(!duplicated.front().diagnostics->front().data.has_value());
  else
    CHECK(false);

  params.context.diagnostics.front().data = lsp::LSPAny("stale");
  const auto fresh = provider.getCodeActions(document, params);
  CHECK(fresh.size() == 1);
  if (fresh.size() != 1) return;
  CHECK(fresh.front().diagnostics.has_value());
  if (!fresh.front().diagnostics.has_value()) return;
  CHECK(fresh.front().diagnostics->size() == 1);
  if (fresh.front().diagnostics->size() != 1) return;
  CHECK(!fresh.front().diagnostics->front().data.has_value());
}

}  // namespace

int main() {
  cancellation_precedes_a_filter_that_excludes_quick_fixes();
  an_action_preserves_the_matching_diagnostic_and_rejects_stale_payloads();
  return cursiv::test::report("code_action_test");
}
