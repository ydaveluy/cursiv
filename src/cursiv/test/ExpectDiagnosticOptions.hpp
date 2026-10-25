// The criteria a diagnostic check is about: each field is one filter. A
// field left absent matches anything; what survives every present one is
// what the expectation asserts on.
#pragma once

#include <lsp/types.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/test/ValidationResult.hpp"

namespace cursiv {
struct AstNode;
}  // namespace cursiv

namespace cursiv::test {

// The criteria a check may state without naming the finding's own words --
// what `expectError` and `expectWarning` take beside the message.
struct ExpectDiagnosticOptionsWithoutContent {
  std::optional<TextOffset> offset{};
  TextOffset length = 0;
  std::optional<lsp::Range> range{};
  std::optional<DiagnosticCode> code{};
  // Matched as a subset: every key present here must match, others are free.
  std::optional<JsonValue> data{};
  const AstNode* node = nullptr;
  // Where a member of `node` was written; absent takes the node's own span.
  std::optional<std::string> property{};
  std::optional<std::size_t> propertyIndex{};
};

struct ExpectDiagnosticOptions {
  std::optional<std::string> message{};
  // The predicate arm: what a regular expression would have decided.
  std::function<bool(std::string_view)> messageMatches{};
  std::optional<DiagnosticSeverity> severity{};
  std::optional<TextOffset> offset{};
  TextOffset length = 0;
  std::optional<lsp::Range> range{};
  std::optional<DiagnosticCode> code{};
  std::optional<JsonValue> data{};
  const AstNode* node = nullptr;
  std::optional<std::string> property{};
  std::optional<std::size_t> propertyIndex{};
};

// Deep equality of the two payloads, `rhs` read as a subset of `lhs`.
[[nodiscard]] bool isDiagnosticDataEqual(const JsonValue& lhs,
                                         const JsonValue& rhs);

[[nodiscard]] std::vector<Diagnostic> filterByOptions(
    const ValidationResult& validationResult,
    const ExpectDiagnosticOptions& options);

void expectNoIssues(const ValidationResult& validationResult,
                    const ExpectDiagnosticOptions& filterOptions = {});
void expectIssue(const ValidationResult& validationResult,
                 const ExpectDiagnosticOptions& filterOptions = {});

void expectError(
    const ValidationResult& validationResult, std::string_view message,
    const ExpectDiagnosticOptionsWithoutContent& filterOptions = {});
void expectError(
    const ValidationResult& validationResult,
    std::function<bool(std::string_view)> messageMatches,
    const ExpectDiagnosticOptionsWithoutContent& filterOptions = {});
void expectWarning(
    const ValidationResult& validationResult, std::string_view message,
    const ExpectDiagnosticOptionsWithoutContent& filterOptions = {});
void expectWarning(
    const ValidationResult& validationResult,
    std::function<bool(std::string_view)> messageMatches,
    const ExpectDiagnosticOptionsWithoutContent& filterOptions = {});

// One line per finding, in the unit the core measures: byte offsets.
[[nodiscard]] std::string printDiagnostics(
    std::span<const Diagnostic> diagnostics);

}  // namespace cursiv::test
