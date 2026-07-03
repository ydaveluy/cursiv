#pragma once

/// Canonical syntax-script helpers and parser-facing diagnostic projection.

#include <algorithm>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include <pegium/core/parser/ParseDiagnostic.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>
#include <pegium/core/syntax-tree/RootCstNode.hpp>

namespace pegium::parser::detail {

/// Internal recovery-script entry. Kept trivially copyable / trivially
/// destructible: a recovery probe pushes entries speculatively and a
/// rewind shrinks the vector hundreds of times in a typical parse,
/// so any non-trivial destructor here multiplies into measurable cost.
///
/// `message` is a non-owning view. Every producer in the codebase
/// passes either `nullptr` (empty view) or a string literal, both of
/// which outlive the parse. The view is converted to an owned
/// `std::string` only at materialisation time
/// (`materialize_syntax_diagnostics`), where the diagnostic crosses
/// the parse boundary.
struct SyntaxScriptEntry {
  ParseDiagnosticKind kind = ParseDiagnosticKind::Deleted;
  TextOffset offset = 0;
  TextOffset beginOffset = 0;
  TextOffset endOffset = 0;
  const grammar::AbstractElement *element = nullptr;
  std::string_view message;

  [[nodiscard]] constexpr bool isSyntax() const noexcept {
    return isSyntaxParseDiagnostic(kind);
  }
};

static_assert(std::is_trivially_copyable_v<SyntaxScriptEntry>,
              "SyntaxScriptEntry must stay trivially copyable so that "
              "speculative recoveryEdits.resize() during recovery probes "
              "is a cheap shrink, not an N×destructor walk.");

[[nodiscard]] std::vector<SyntaxScriptEntry>
normalize_syntax_script(std::span<const SyntaxScriptEntry> entries);

[[nodiscard]] std::vector<ParseDiagnostic>
materialize_syntax_diagnostics(std::span<const SyntaxScriptEntry> entries);

inline bool
has_syntax_diagnostic(std::span<const SyntaxScriptEntry> entries) {
  return std::ranges::any_of(entries, [](const auto &entry) {
    return entry.isSyntax();
  });
}

inline bool
has_parse_diagnostic_kind(std::span<const SyntaxScriptEntry> entries,
                          ParseDiagnosticKind kind) {
  return std::ranges::any_of(entries, [kind](const auto &entry) {
    return entry.kind == kind;
  });
}

inline CstNodeView first_recovered_node(const RootCstNode &cst) {
  for (NodeId id = 0;; ++id) {
    const auto node = cst.get(id);
    if (!node.valid()) {
      return {};
    }
    if (node.isRecovered()) {
      return node;
    }
  }
}

inline void
append_syntax_summary_entry(std::vector<SyntaxScriptEntry> &entries,
                            const RootCstNode *cst, TextOffset parsedLength,
                            TextOffset lastVisibleCursorOffset,
                            TextOffset failureVisibleCursorOffset,
                            TextOffset inputSize, bool hasRecovered,
                            bool fullMatch, bool recoveryEnabled) {
  if (!fullMatch) {
    if (has_parse_diagnostic_kind(entries, ParseDiagnosticKind::Incomplete)) {
      return;
    }
    const auto incompleteOffset =
        std::max(parsedLength, failureVisibleCursorOffset);
    const auto safeInputSize = std::max(parsedLength, inputSize);
    // Unconsumed input is reported as a ranged "Unexpected input." diagnostic
    // spanning the tail. With recovery enabled this is gated on a committed
    // recovery (the precise edit diagnostics carry the detail); when recovery is
    // disabled there are no edit diagnostics, so the strict parse still surfaces
    // a visible error region instead of a zero-width marker.
    const auto trailingUnexpectedInput =
        parsedLength < safeInputSize && (hasRecovered || !recoveryEnabled);
    const auto recoveredTailBegin =
        std::min(parsedLength, lastVisibleCursorOffset);
    entries.push_back({.kind = ParseDiagnosticKind::Incomplete,
                       .offset = incompleteOffset,
                       .beginOffset =
                           trailingUnexpectedInput ? recoveredTailBegin
                                                   : incompleteOffset,
                       .endOffset =
                           trailingUnexpectedInput ? safeInputSize
                                                   : incompleteOffset,
                       .element = nullptr,
                       .message = trailingUnexpectedInput
                                      ? std::string_view{"Unexpected input."}
                                      : std::string_view{}});
    return;
  }
  if (has_syntax_diagnostic(entries)) {
    return;
  }
  if (!hasRecovered || cst == nullptr) {
    return;
  }
  if (const auto recovered = first_recovered_node(*cst); recovered.valid()) {
    entries.push_back({.kind = ParseDiagnosticKind::Recovered,
                       .offset = recovered.getBegin(),
                       .beginOffset = recovered.getBegin(),
                       .endOffset = recovered.getEnd(),
                       .element = nullptr,
                       .message = {}});
  }
}

} // namespace pegium::parser::detail
