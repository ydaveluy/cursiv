#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <pegium/core/parser/ExpectApi.hpp>
#include <pegium/core/parser/ParseDiagnostic.hpp>
#include <pegium/core/parser/ParseOptions.hpp>
#include <pegium/core/syntax-tree/AstArena.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/syntax-tree/RootCstNode.hpp>
#include <pegium/core/text/TextSnapshot.hpp>
#include <pegium/core/utils/Cancellation.hpp>

namespace pegium::grammar {
struct ParserRule;
}

namespace pegium::parser {

/// Summary of the last recovery window selected by the parser.
struct RecoveryWindowReport {
  /// Start offset of the recovery window.
  TextOffset beginOffset = 0;

  /// Furthest cursor offset that triggered the window.
  TextOffset maxCursorOffset = 0;

  /// Visible tokens included before the failure point.
  std::uint32_t backwardTokenCount = 0;

  /// Visible tokens allowed while replaying the editable recovery window.
  std::uint32_t forwardTokenCount = 0;
};

/// Aggregate counters describing how recovery behaved for one parse.
struct RecoveryReport {
  /// Whether at least one recovery window was accepted.
  bool hasRecovered = false;

  /// Whether recovery produced a full match of the input.
  bool fullRecovered = false;

  /// Number of accepted recovery windows.
  std::uint32_t recoveryCount = 0;

  /// Total number of recovery windows that were evaluated.
  std::uint32_t recoveryWindowsTried = 0;

  /// Number of strict parse runs executed, including retries.
  std::uint32_t strictParseRuns = 0;

  /// Total number of recovery attempt runs, strict or editable.
  std::uint32_t recoveryAttemptRuns = 0;

  /// Number of edits kept in the selected recovery attempt.
  std::uint32_t recoveryEdits = 0;

  /// OrderedChoice recovery cache hits during this parse.
  std::uint64_t choiceRecoverCacheHits = 0;

  /// OrderedChoice recovery cache misses during this parse.
  std::uint64_t choiceRecoverCacheMisses = 0;

  /// Last accepted recovery window, if recovery happened.
  std::optional<RecoveryWindowReport> lastRecoveryWindow;
};

/// Complete parser output for one document parse.
struct ParseResult {
  /// Arena that owns every AstNode reachable from `value`.
  ///
  /// Declared BEFORE `cst` so that:
  /// - the implicit move assignment destroys the previous `astArena` first,
  ///   while the previous `cst` (and its pool) is still alive;
  /// - the destructor body explicitly resets `astArena` first to satisfy the
  ///   same ordering against the implicit member destruction.
  /// AST nodes are allocated from the CST root's monotonic buffer pool, so
  /// the arena MUST be torn down before the CST.
  std::unique_ptr<AstArena> astArena;

  /// Concrete syntax tree built from the selected parse attempt.
  std::unique_ptr<RootCstNode> cst;

  /// AST root produced by conversion, or nullptr when conversion failed.
  ///
  /// Non-owning: the root and every reachable child are stored inside
  /// `astArena`.
  AstNode *value = nullptr;

  /// Collected reference handles extracted from the parsed value.
  std::vector<ReferenceHandle> references;

  /// Syntax and conversion diagnostics produced during parsing.
  std::vector<ParseDiagnostic> parseDiagnostics;

  /// Recovery counters for this parse.
  RecoveryReport recoveryReport;

  /// Length consumed by the selected parse attempt.
  TextOffset parsedLength = 0;

  /// Furthest visible-token end reached by the selected parse attempt.
  TextOffset lastVisibleCursorOffset = 0;

  /// Visible-token end closest to the initial syntax failure.
  TextOffset failureVisibleCursorOffset = 0;

  /// Furthest raw cursor offset reached by the selected parse attempt.
  TextOffset maxCursorOffset = 0;

  /// Whether the selected parse attempt matched the full input.
  bool fullMatch = false;

  ParseResult() = default;
  ParseResult(const ParseResult &) = delete;
  ParseResult &operator=(const ParseResult &) = delete;
  ParseResult(ParseResult &&) = default;
  ParseResult &operator=(ParseResult &&) = default;

  /// Reset `astArena` first so AST node destructors run while the CST pool
  /// is still alive. The implicit member destruction afterwards releases
  /// `cst` (and its pool) and finds an already-empty `astArena`.
  ~ParseResult() noexcept { astArena.reset(); }
};

/// Abstract parser interface implemented by generated or hand-written parsers.
class Parser {
public:
  virtual ~Parser() noexcept = default;

  /// Returns the entry parser rule of this grammar.
  ///
  /// Pegium uses the entry rule to bootstrap AST reflection before any
  /// document build starts, so parsers are also the source of truth for the
  /// statically known AST type hierarchy.
  [[nodiscard]] virtual const grammar::ParserRule &
  getEntryRule() const noexcept = 0;

  /// Parses one immutable text snapshot and returns the full parse result.
  ///
  /// This is the canonical parser entrypoint used both by workspace documents
  /// and standalone tools.
  [[nodiscard]] virtual ParseResult
  parse(text::TextSnapshot text,
        const utils::CancellationToken &cancelToken = {}) const = 0;

  /// Convenience overload for standalone callers parsing borrowed text.
  ///
  /// The input is copied into a `TextSnapshot` before dispatching to the
  /// virtual snapshot-based parser entrypoint.
  [[nodiscard]] ParseResult
  parse(std::string_view text,
        const utils::CancellationToken &cancelToken = {}) const {
    return parse(text::TextSnapshot::copy(text), cancelToken);
  }

  /// Computes parser expectations at `offset` inside `text`.
  ///
  /// The returned frontier contains the grammar paths that can continue parsing
  /// at the requested anchor, which makes it suitable for diagnostics and
  /// completion providers.
  [[nodiscard]] virtual ExpectResult expect(
      std::string_view text, TextOffset offset,
      const utils::CancellationToken &cancelToken = {}) const = 0;
};

} // namespace pegium::parser
