#include "cursiv/test/ExpectDiagnosticOptions.hpp"

#include <lsp/types.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/text/TextRange.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectationError.hpp"
#include "cursiv/test/RangeUtils.hpp"

namespace cursiv::test {

namespace {

// The node's own span, or the record that wrote one of its members; a miss
// here is the caller's mistake, not a filter that matches nothing.
std::optional<TextRange> anchorOf(const ValidationResult& result,
                                  const ExpectDiagnosticOptions& options) {
  const Document& document = *result.document;
  const AstNode& node = *options.node;
  if (node.cstNodeId == kNoNode || document.parseResult.value == nullptr)
    return std::nullopt;
  const Cst& cst = document.parseResult.cst();
  if (!options.property.has_value()) {
    const CstNode& own = cst[node.cstNodeId];
    return TextRange{own.begin, own.end};
  }
  const std::optional<CstNodeView> at = find_node_for_feature(
      CstNodeView{&cst, node.cstNodeId}, *options.property,
      options.propertyIndex.value_or(0));
  if (!at.has_value()) return std::nullopt;
  return TextRange{at->begin(), at->end()};
}

}  // namespace

bool isDiagnosticDataEqual(const JsonValue& lhs, const JsonValue& rhs) {
  // The serialization is deterministic, which makes it the deep comparison.
  if (lhs.toJsonString() == rhs.toJsonString()) return true;
  if (!lhs.isObject() || !rhs.isObject()) return false;
  for (const auto& [key, value] : rhs.object()) {
    const auto at = lhs.object().find(key);
    if (at == lhs.object().end() || !isDiagnosticDataEqual(at->second, value))
      return false;
  }
  return true;
}

std::vector<Diagnostic> filterByOptions(
    const ValidationResult& validationResult,
    const ExpectDiagnosticOptions& options) {
  const TextDocument& text = validationResult.document->textDocument();
  const auto rangeOf = [&text](const Diagnostic& d) {
    return to_lsp(text.rangeAt(d.range.begin, d.range.end));
  };

  std::optional<lsp::Range> anchor;
  if (options.node != nullptr) {
    const std::optional<TextRange> span = anchorOf(validationResult, options);
    if (!span.has_value())
      throw ExpectationError("cursiv: cannot find the node!");
    anchor = to_lsp(text.rangeAt(span->begin, span->end));
  }

  std::optional<lsp::Range> outer;
  if (options.offset.has_value())
    outer =
        to_lsp(text.rangeAt(*options.offset, *options.offset + options.length));

  std::vector<Diagnostic> kept;
  for (const Diagnostic& d : validationResult.diagnostics) {
    if (anchor.has_value() && !isRangeEqual(*anchor, rangeOf(d))) continue;
    if (outer.has_value() && !isRangeEqual(*outer, rangeOf(d))) continue;
    if (options.range.has_value() && !isRangeEqual(*options.range, rangeOf(d)))
      continue;
    if (options.code.has_value() && d.code != options.code) continue;
    if (options.data.has_value() &&
        !(d.data.has_value() && isDiagnosticDataEqual(*d.data, *options.data)))
      continue;
    if (options.message.has_value() && d.message != *options.message) continue;
    if (options.messageMatches && !options.messageMatches(d.message)) continue;
    if (options.severity.has_value() && d.severity != *options.severity)
      continue;
    kept.push_back(d);
  }
  return kept;
}

void expectNoIssues(const ValidationResult& validationResult,
                    const ExpectDiagnosticOptions& filterOptions) {
  const std::vector<Diagnostic> filtered =
      filterByOptions(validationResult, filterOptions);
  expectedFunction(std::to_string(filtered.size()), "0",
                   "expected no issues in '" + validationResult.document->uri +
                       "', but found " + std::to_string(filtered.size()) +
                       ":\n" + printDiagnostics(filtered));
}

void expectIssue(const ValidationResult& validationResult,
                 const ExpectDiagnosticOptions& filterOptions) {
  const std::vector<Diagnostic> filtered =
      filterByOptions(validationResult, filterOptions);
  expectedFunction(
      filtered.empty() ? "false" : "true", "true",
      "found no issues in '" + validationResult.document->uri + "'");
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
                                 .propertyIndex = filterOptions.propertyIndex};
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
