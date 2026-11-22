#include "cursiv/ExpectDiagnosticOptions.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

namespace {

CstNodeView anchorOf(const Document& document,
                     const ExpectDiagnosticOptions& options) {
  const AstNode& node = *options.node;
  if (node.getDocument() != &document) return {};
  if (!options.property) return cstNodeOf(node, &document);
  const auto index = options.propertyIndex.value_or(0);
  if (index == 0)
    return findNodeForProperty(node, *options.property, 0, &document);
  const auto sites =
      findNodesForProperty(node, *options.property, &document);
  return index < sites.size() ? sites[index] : CstNodeView{};
}

bool sameNumber(const JsonValue& lhs, const JsonValue& rhs) {
  if (!lhs.isNumber()) return false;
  if (lhs.isInteger() && rhs.isInteger()) return lhs.integer() == rhs.integer();
  if (!lhs.isInteger() && !rhs.isInteger()) return lhs.number() == rhs.number();
  const auto integer = lhs.isInteger() ? lhs.integer() : rhs.integer();
  const double decimal = lhs.isInteger() ? rhs.number() : lhs.number();
  return decimal >= -0x1p63 && decimal < 0x1p63 &&
         std::trunc(decimal) == decimal &&
         static_cast<std::int64_t>(decimal) == integer;
}

}  // namespace

bool isDiagnosticDataEqual(const JsonValue& lhs, const JsonValue& rhs) {
  std::vector<std::pair<const JsonValue*, const JsonValue*>> pending;
  const JsonValue* held = &lhs;
  const JsonValue* wanted = &rhs;
  for (;;) {
    if (wanted->isObject()) {
      if (!held->isObject()) return false;
      for (const auto& [key, value] : wanted->object()) {
        const auto at = held->object().find(key);
        if (at == held->object().end()) return false;
        pending.emplace_back(&at->second, &value);
      }
    } else if (wanted->isArray()) {
      if (!held->isArray() || wanted->array().size() > held->array().size())
        return false;
      for (std::size_t i = wanted->array().size(); i > 0; --i)
        pending.emplace_back(&held->array()[i - 1], &wanted->array()[i - 1]);
    } else if (wanted->isString()) {
      if (!held->isString() || held->string() != wanted->string()) return false;
    } else if (wanted->isNumber()) {
      if (!sameNumber(*held, *wanted)) return false;
    } else if (wanted->isBoolean()) {
      if (!held->isBoolean() || held->boolean() != wanted->boolean())
        return false;
    } else if (!held->isNull()) {
      return false;
    }
    if (pending.empty()) return true;
    std::tie(held, wanted) = pending.back();
    pending.pop_back();
  }
}

namespace {

auto matchingDiagnostics(const Document& document,
                         std::span<const Diagnostic> diagnostics,
                         const ExpectDiagnosticOptions& options) {
  const TextDocument& text = document.textDocument();
  if (options.property && !options.node)
    throw ExpectationError("a property filter requires a node");
  if (options.propertyIndex && !options.property)
    throw ExpectationError("a property index requires a property");
  if (options.length && !options.offset)
    throw ExpectationError("a diagnostic length requires an offset");
  CstNodeView anchor;
  if (options.node) {
    anchor = anchorOf(document, options);
    if (!anchor)
      throw ExpectationError(
          "cannot find the requested node or property in the document");
  }

  const auto matches = [anchor, &text, &options](const Diagnostic& d) {
    if (anchor &&
        (d.range.begin != anchor.begin() || d.range.end != anchor.end()))
      return false;
    if (options.offset &&
        (d.range.begin != *options.offset || d.range.end < d.range.begin ||
         d.range.end - d.range.begin != options.length))
      return false;
    if (options.range.has_value() &&
        !isRangeEqual(*options.range,
                      toLsp(text.rangeAt(d.range.begin, d.range.end))))
      return false;
    if (options.source && d.source != *options.source) return false;
    if (options.tags && !std::ranges::is_permutation(d.tags, *options.tags))
      return false;
    if (options.code.has_value() && d.code != options.code) return false;
    if (options.data.has_value() &&
        !(d.data.has_value() && isDiagnosticDataEqual(*d.data, *options.data)))
      return false;
    if (options.message.has_value() && d.message != *options.message)
      return false;
    if (options.messageMatches && !options.messageMatches(d.message))
      return false;
    if (options.severity.has_value() && d.severity != *options.severity)
      return false;
    return true;
  };
  return diagnostics | std::views::filter(matches);
}

}  // namespace

std::vector<Diagnostic> filterByOptions(
    const ValidationResult& validationResult,
    const ExpectDiagnosticOptions& options) {
  std::vector<Diagnostic> matched;
  for (const auto& d : matchingDiagnostics(
           *validationResult.document, validationResult.diagnostics, options))
    matched.push_back(d);
  return matched;
}

namespace {
void check_issues(const Document& doc, std::span<const Diagnostic> diagnostics,
                  const ExpectDiagnosticOptions& options, bool wantIssue) {
  auto matched = matchingDiagnostics(doc, diagnostics, options);
  if (wantIssue) {
    expectedFunction(matched.empty() ? "false" : "true", "true",
                     "found no issues in '" + doc.uri + "'");
    return;
  }
  std::string report;
  std::size_t count = 0;
  for (const Diagnostic& d : matched) {
    ++count;
    report += '\n' + printDiagnostics({&d, 1});
  }
  expectedFunction(std::to_string(count), "0",
                   "unexpected diagnostics in '" + doc.uri + "'" + report);
}
}  // namespace
void expectNoIssues(const ValidationResult& result,
                    const ExpectDiagnosticOptions& options) {
  check_issues(*result.document, result.diagnostics, options, false);
}
void expectIssue(const ValidationResult& result,
                 const ExpectDiagnosticOptions& options) {
  check_issues(*result.document, result.diagnostics, options, true);
}
void expectNoIssues(const Document& doc,
                    const ExpectDiagnosticOptions& options) {
  check_issues(doc, doc.diagnostics, options, false);
}
void expectIssue(const Document& doc, const ExpectDiagnosticOptions& options) {
  check_issues(doc, doc.diagnostics, options, true);
}

namespace {

void expectContent(const ValidationResult& validationResult,
                   ExpectDiagnosticOptions options,
                   DiagnosticSeverity severity) {
  options.severity = severity;
  expectIssue(validationResult, options);
}

// Constructed, never assigned: copy-assigning the `JsonValue` payload walks
// the variant's assignment vtable, which g++ reads as a delete of an object it
// cannot see allocated.
ExpectDiagnosticOptions withFilters(
    const ExpectDiagnosticOptionsWithoutContent& filterOptions) {
  return ExpectDiagnosticOptions{.offset = filterOptions.offset,
                                 .length = filterOptions.length,
                                 .range = filterOptions.range,
                                 .code = filterOptions.code,
                                 .data = filterOptions.data,
                                 .node = filterOptions.node,
                                 .property = filterOptions.property,
                                 .propertyIndex = filterOptions.propertyIndex,
                                 .source = filterOptions.source,
                                 .tags = filterOptions.tags};
}

}  // namespace

void expectError(const ValidationResult& validationResult,
                 std::string_view message,
                 const ExpectDiagnosticOptionsWithoutContent& filterOptions) {
  ExpectDiagnosticOptions options = withFilters(filterOptions);
  options.message = std::string(message);
  expectContent(validationResult, std::move(options),
                DiagnosticSeverity::Error);
}

void expectError(const ValidationResult& validationResult,
                 std::function<bool(std::string_view)> messageMatches,
                 const ExpectDiagnosticOptionsWithoutContent& filterOptions) {
  ExpectDiagnosticOptions options = withFilters(filterOptions);
  options.messageMatches = std::move(messageMatches);
  expectContent(validationResult, std::move(options),
                DiagnosticSeverity::Error);
}

void expectWarning(const ValidationResult& validationResult,
                   std::string_view message,
                   const ExpectDiagnosticOptionsWithoutContent& filterOptions) {
  ExpectDiagnosticOptions options = withFilters(filterOptions);
  options.message = std::string(message);
  expectContent(validationResult, std::move(options),
                DiagnosticSeverity::Warning);
}

void expectWarning(const ValidationResult& validationResult,
                   std::function<bool(std::string_view)> messageMatches,
                   const ExpectDiagnosticOptionsWithoutContent& filterOptions) {
  ExpectDiagnosticOptions options = withFilters(filterOptions);
  options.messageMatches = std::move(messageMatches);
  expectContent(validationResult, std::move(options),
                DiagnosticSeverity::Warning);
}

std::string printDiagnostics(std::span<const Diagnostic> diagnostics) {
  std::string out;
  for (const Diagnostic& d : diagnostics) {
    if (!out.empty()) out += '\n';
    out += "offset " + std::to_string(d.range.begin) + ".." +
           std::to_string(d.range.end) + ": " + d.message;
  }
  return out;
}

}  // namespace cursiv::test
