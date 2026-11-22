#include "cursiv/actions/JsonBridge.hpp"

#include <lsp/json/json.h>
#include <lsp/types.h>

#include <cstdint>
#include <utility>

#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::detail {

namespace {

// The two scales match by name, not by number: `Severity` is the wire
// value, `lsp::DiagnosticSeverity` a zero-based index into the table
// that emits it -- a cast would shift every squiggle one level.
[[nodiscard]] lsp::DiagnosticSeverity to_lsp_severity(
    DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Warning:
      return lsp::DiagnosticSeverity::Warning;
    case DiagnosticSeverity::Information:
      return lsp::DiagnosticSeverity::Information;
    case DiagnosticSeverity::Hint:
      return lsp::DiagnosticSeverity::Hint;
    case DiagnosticSeverity::Error:
      break;
  }
  return lsp::DiagnosticSeverity::Error;
}

// The same name-not-number rule as the severities above: `lsp::DiagnosticTag`
// is a zero-based index and the wire value it emits through is one past it.
[[nodiscard]] lsp::DiagnosticTag to_lsp_tag(DiagnosticTag tag) {
  switch (tag) {
    case DiagnosticTag::Deprecated:
      return lsp::DiagnosticTag::Deprecated;
    case DiagnosticTag::Unnecessary:
      break;
  }
  return lsp::DiagnosticTag::Unnecessary;
}

}  // namespace

// The framework's integer is 32-bit on the wire; a payload value outside it
// travels as a decimal, exact up to 2^53 in magnitude. Past that the client
// reads it rounded, as a JavaScript client reads any number.
lsp::LSPAny to_lsp_any(const JsonValue& value) {
  if (value.isBoolean()) return lsp::LSPAny(value.boolean());
  if (value.isInteger()) {
    const std::int64_t i = value.integer();
    if (i >= INT32_MIN && i <= INT32_MAX)
      return lsp::LSPAny(static_cast<lsp::json::Integer>(i));
    return lsp::LSPAny(static_cast<lsp::json::Decimal>(i));
  }
  if (value.isNumber()) return lsp::LSPAny(value.number());
  if (value.isString()) return lsp::LSPAny(lsp::json::String(value.string()));
  if (value.isArray()) {
    lsp::json::Array array;
    array.reserve(value.array().size());
    for (const JsonValue& element : value.array())
      array.push_back(to_lsp_any(element));
    return lsp::LSPAny(std::move(array));
  }
  if (value.isObject()) {
    // The keys are unique already: each is appended, never looked up.
    lsp::json::Object object;
    object.reserve(value.object().size());
    for (const auto& [key, element] : value.object())
      object.append(key, to_lsp_any(element));
    return lsp::LSPAny(std::move(object));
  }
  return lsp::LSPAny(lsp::json::Null{});
}

lsp::Diagnostic to_lsp_diagnostic(const Document& document, const Diagnostic& d,
                                  const Documents& documents) {
  lsp::Diagnostic published;
  published.range =
      toLsp(document.textDocument().rangeAt(d.range.begin, d.range.end));
  published.message = d.message;
  published.severity = to_lsp_severity(d.severity);
  if (!d.source.empty()) published.source = std::string(d.source);
  // `DiagnosticCode` is LSP's `integer | string`; the wire narrows the
  // integer to `int` here, the one place that handles both shapes.
  if (d.code.has_value()) {
    if (d.code->isString())
      published.code = d.code->string();
    else
      published.code = static_cast<int>(d.code->integer());
  }
  // `data` comes back untouched in `codeAction` requests: it carries the
  // quick fixes.
  if (d.data.has_value()) published.data = to_lsp_any(*d.data);
  if (d.codeDescription.has_value()) {
    lsp::CodeDescription description;
    description.href = lsp::Uri::parse(*d.codeDescription);
    published.codeDescription = std::move(description);
  }
  if (!d.tags.empty()) {
    std::vector<lsp::DiagnosticTagEnum> tags;
    tags.reserve(d.tags.size());
    for (const DiagnosticTag tag : d.tags) tags.emplace_back(to_lsp_tag(tag));
    published.tags = std::move(tags);
  }
  // Each row names its own document (an empty `uri` means this one), whose
  // index places it. An unheld document answers nothing.
  if (!d.relatedInformation.empty()) {
    std::vector<lsp::DiagnosticRelatedInformation> related;
    related.reserve(d.relatedInformation.size());
    for (const DiagnosticRelatedInformation& row : d.relatedInformation) {
      const std::shared_ptr<Document> held =
          row.uri.empty() ? nullptr : documents.getDocument(row.uri);
      const Document* target = row.uri.empty() ? &document : held.get();
      if (target == nullptr) continue;
      lsp::DiagnosticRelatedInformation info;
      info.location.uri = lsp::Uri::parse(target->uri);
      info.location.range = toLsp(
          target->textDocument().rangeAt(row.range.begin, row.range.end));
      info.message = row.message;
      related.push_back(std::move(info));
    }
    if (!related.empty()) published.relatedInformation = std::move(related);
  }
  return published;
}

}  // namespace cursiv::detail
