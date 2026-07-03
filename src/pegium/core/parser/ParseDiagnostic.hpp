#pragma once

/// Parser diagnostic record anchored to a source range, plus its
/// parser-facing normalization helper.

#include <ostream>
#include <span>
#include <string>
#include <vector>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/parser/ParseDiagnosticKind.hpp>
#include <pegium/core/syntax-tree/CstNode.hpp>

namespace pegium::parser {
/// One parser diagnostic anchored to a source range and optional grammar element.
struct ParseDiagnostic {
  /// Diagnostic category.
  ParseDiagnosticKind kind = ParseDiagnosticKind::Deleted;

  /// Parse or recovery position used for ranking and expectation lookup.
  TextOffset offset = 0;

  /// Start offset of the affected source range.
  TextOffset beginOffset = 0;

  /// End offset of the affected source range.
  TextOffset endOffset = 0;

  /// Grammar element that triggered the diagnostic, when known.
  const grammar::AbstractElement *element = nullptr;

  /// Optional human-readable detail, mainly used for conversion failures.
  std::string message;

  /// Returns whether this diagnostic comes from syntax parsing or recovery.
  [[nodiscard]] constexpr bool isSyntax() const noexcept {
    return isSyntaxParseDiagnostic(kind);
  }

private:
  friend std::ostream &operator<<(std::ostream &os,
                                  const ParseDiagnostic &diagnostic) {
    os << "ParseDiagnostic{kind=" << diagnostic.kind
       << ", offset=" << diagnostic.offset
       << ", begin=" << diagnostic.beginOffset
       << ", end=" << diagnostic.endOffset;
    if (diagnostic.element != nullptr) {
      os << ", element=" << *diagnostic.element;
    }
    if (!diagnostic.message.empty()) {
      os << ", message=\"" << diagnostic.message << "\"";
    }
    os << "}";
    return os;
  }
};

/// Coalesces adjacent recovery diagnostics into stable parser-facing output.
///
/// This keeps delete runs contiguous and merges same-offset inserted
/// expectations so downstream consumers do not need to reconstruct recovery
/// edit structure on their own.
[[nodiscard]] std::vector<ParseDiagnostic>
normalizeParseDiagnostics(std::span<const ParseDiagnostic> diagnostics);

} // namespace pegium::parser
