#pragma once

/// Reusable, GoogleTest-coupled language test harness for pegium-based
/// languages. Parse a marked-up source string,
/// then assert on validation and LSP features (completion, hover, go-to-
/// definition, references, highlight, formatting, symbols, folding).
///
/// Marker syntax in the `text` of every `Expected…`:
///   - `<|>`     a cursor index (multiple allowed; pick one with `.index`)
///   - `<| … |>` a range (used for expected ranges / folding)
///
/// Public surface only — the LSP feature dispatch is hidden inside the compiled
/// `pegium::testing` library, so consumers never include pegium internals.
/// Link `pegium::testing` (and `GTest::gtest_main`) from your test target.

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <lsp/types.h>

#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/testing/LspProbe.hpp>

namespace pegium::testing {

/// Owns a shared LSP runtime for a test and lets you register languages on it.
/// Construct one per test (cheap) or call `clear()` between cases. Movable, so a
/// helper can build and return a ready-to-use workspace; not copyable.
class TestWorkspace {
public:
  TestWorkspace();
  ~TestWorkspace();
  TestWorkspace(const TestWorkspace &) = delete;
  TestWorkspace &operator=(const TestWorkspace &) = delete;
  TestWorkspace(TestWorkspace &&) noexcept;
  TestWorkspace &operator=(TestWorkspace &&) noexcept;

  /// Builds a workspace whose shared runtime is a language's own
  /// `SharedServices` subclass (must be default-constructible), so
  /// `shared<TShared>()` hands the language its custom shared services.
  template <typename TShared>
    requires std::derived_from<TShared, SharedServices> &&
             std::default_initializable<TShared>
  [[nodiscard]] static TestWorkspace withShared() {
    return TestWorkspace(std::make_unique<TShared>());
  }

  [[nodiscard]] pegium::SharedServices &shared() noexcept { return *_shared; }

  /// Typed view of the shared runtime built via `withShared<TShared>()`.
  template <typename TShared> [[nodiscard]] TShared &shared() noexcept {
    assert(dynamic_cast<TShared *>(_shared.get()) != nullptr &&
           "shared<TShared>() needs a workspace from withShared<TShared>()");
    return static_cast<TShared &>(*_shared);
  }

  /// Registers a language's services (e.g. the result of your
  /// `create<Lang>Services(ws.shared<TShared>())`) on the shared runtime.
  void registerLanguage(std::unique_ptr<pegium::Services> services);

  /// Drops all open documents so the next case starts clean.
  void clear();

private:
  explicit TestWorkspace(std::unique_ptr<pegium::SharedServices> shared);
  std::unique_ptr<pegium::SharedServices> _shared;
};

/// Builds a document from `text` through the full pipeline (parse → index →
/// scopes → link → validate) and returns it. Reach the underlying runtime with
/// `ws.shared()` when you need to call the headless feature API directly.
std::shared_ptr<workspace::Document>
parse(TestWorkspace &ws, std::string_view languageId, std::string text,
      std::optional<std::string> documentUri = std::nullopt);

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

/// One expected diagnostic. `message` is matched as a substring; `severity` and
/// the optional `<| |>` range index (into the marked ranges) are matched when set.
struct ExpectedDiagnostic {
  std::string message;
  std::optional<DiagnosticSeverity> severity;
  std::optional<std::size_t> rangeIndex;
};

struct ExpectedValidation {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  std::vector<ExpectedDiagnostic> diagnostics;
  std::function<void(const std::vector<pegium::Diagnostic> &)> check;
};

/// Parses `text` and asserts the produced diagnostics. With `check` set, calls
/// it with the raw diagnostics; otherwise every entry in `diagnostics` must be
/// matched by exactly one produced diagnostic and the counts must be equal.
std::shared_ptr<workspace::Document>
expectValidation(TestWorkspace &ws, std::string_view languageId,
                 const ExpectedValidation &expected);

// ---------------------------------------------------------------------------
// Validation — validate() plus expect{Error,Warning,…} assertions
// ---------------------------------------------------------------------------

/// A parsed document plus its diagnostics. Produce one with `validate()`,
/// then assert with the
/// `expect{Error,Warning,Issue,NoIssues}` helpers below.
struct ValidationResult {
  std::shared_ptr<workspace::Document> document;
  std::vector<pegium::Diagnostic> diagnostics;
};

/// Parses `text` through the full pipeline and returns the document plus its
/// diagnostics. Plain source —
/// no cursor/range markers are interpreted.
[[nodiscard]] ValidationResult validate(TestWorkspace &ws, std::string_view languageId,
                          std::string text,
                          std::optional<std::string> documentUri = std::nullopt);

/// Narrows which diagnostics an `expect…` assertion considers. Every field
/// that is set must match.
struct DiagnosticFilter {
  std::optional<DiagnosticSeverity> severity;
  std::optional<DiagnosticCode> code;
  std::optional<TextOffset> offset; ///< diagnostic span must contain this offset
  std::optional<std::pair<TextOffset, TextOffset>> range; ///< exact [begin, end)
  std::function<bool(const pegium::Diagnostic &)> predicate;
};

/// Asserts no diagnostic matches `filter` (with an empty filter: no diagnostics).
void expectNoIssues(const ValidationResult &result,
                    const DiagnosticFilter &filter = {});

/// Asserts at least one diagnostic matches `filter`.
void expectIssue(const ValidationResult &result,
                 const DiagnosticFilter &filter = {});

/// Asserts at least one Error diagnostic matches `filter` and whose message
/// contains `message` (substring).
void expectError(const ValidationResult &result, std::string_view message,
                 const DiagnosticFilter &filter = {});

/// Asserts at least one Warning diagnostic matches `filter` and whose message
/// contains `message` (substring).
void expectWarning(const ValidationResult &result, std::string_view message,
                   const DiagnosticFilter &filter = {});

// ---------------------------------------------------------------------------
// Code actions — testCodeAction assertions
// ---------------------------------------------------------------------------

/// Outcome of `testCodeAction`: the built document, all diagnostics, the single
/// diagnostic carrying the requested code, and the resolved quick-fix (if any).
struct CodeActionResult {
  std::shared_ptr<workspace::Document> document;
  std::vector<pegium::Diagnostic> diagnosticsAll;
  std::optional<pegium::Diagnostic> diagnosticRelevant;
  std::optional<::lsp::CodeAction> action;
};

/// Parses `input`, expects exactly one diagnostic whose stable `code` equals
/// `diagnosticCode`, requests code actions for it, and — when `outputAfterFix`
/// is given — asserts there is exactly one `CodeAction` whose edit transforms
/// the document into `outputAfterFix`.
[[nodiscard]] CodeActionResult
testCodeAction(TestWorkspace &ws, std::string_view languageId, std::string input,
               std::string_view diagnosticCode,
               std::optional<std::string> outputAfterFix = std::nullopt,
               std::optional<std::string> documentUri = std::nullopt);

// ---------------------------------------------------------------------------
// Semantic tokens — highlight + expectSemanticToken assertions
// ---------------------------------------------------------------------------

/// The decoded semantic tokens of a document plus the `<| |>` ranges from the
/// source.
struct DecodedSemanticTokens {
  std::vector<DecodedSemanticToken> tokens;
  std::vector<std::pair<TextOffset, TextOffset>> ranges;
};

/// Parses `text`, runs the language's semantic-token provider, and returns the
/// decoded tokens (type names resolved via the provider legend) plus the marked
/// ranges. Fails if the language has no semantic-token provider.
[[nodiscard]] DecodedSemanticTokens
highlight(TestWorkspace &ws, std::string_view languageId, std::string text,
          std::optional<std::string> documentUri = std::nullopt);

/// Selects the expected token: its type name and the marked range it must cover.
struct ExpectedSemanticToken {
  std::size_t rangeIndex = 0;
  std::string tokenType;
};

/// Asserts exactly one decoded token has `tokenType` and exactly covers the
/// marked range `expected.rangeIndex`.
void expectSemanticToken(const DecodedSemanticTokens &decoded,
                         const ExpectedSemanticToken &expected);

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------

struct ExpectedCompletion {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  std::size_t index = 0;
  std::vector<std::string> expectedItems;
  std::function<void(const ::lsp::CompletionList &)> check;
  std::function<std::string(const ::lsp::CompletionItem &)> itemToString;
};

std::shared_ptr<workspace::Document>
expectCompletion(TestWorkspace &ws, std::string_view languageId,
                 const ExpectedCompletion &expected);

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

struct ExpectedHover {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  std::size_t index = 0;
  std::optional<std::string> hover;
  std::function<void(const std::optional<::lsp::Hover> &)> check;
};

std::shared_ptr<workspace::Document>
expectHover(TestWorkspace &ws, std::string_view languageId,
            const ExpectedHover &expected);

// ---------------------------------------------------------------------------
// Go to definition
// ---------------------------------------------------------------------------

struct ExpectedGoToDefinition {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  std::size_t index = 0;
  std::variant<std::monostate, std::size_t, std::vector<std::size_t>> rangeIndex;
};

std::shared_ptr<workspace::Document>
expectGoToDefinition(TestWorkspace &ws, std::string_view languageId,
                     const ExpectedGoToDefinition &expected);

// ---------------------------------------------------------------------------
// Find references
// ---------------------------------------------------------------------------

struct ExpectedFindReferences {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  bool includeDeclaration = false;
};

std::shared_ptr<workspace::Document>
expectFindReferences(TestWorkspace &ws, std::string_view languageId,
                     const ExpectedFindReferences &expected);

// ---------------------------------------------------------------------------
// Document highlight
// ---------------------------------------------------------------------------

struct ExpectedHighlight {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  std::size_t index = 0;
  std::variant<std::monostate, std::size_t, std::vector<std::size_t>> rangeIndex;
};

std::shared_ptr<workspace::Document>
expectHighlight(TestWorkspace &ws, std::string_view languageId,
                const ExpectedHighlight &expected);

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

struct ExpectedFormatting {
  std::string before;
  std::string after;
  std::optional<::lsp::Range> range;
  ::lsp::FormattingOptions options{.tabSize = 4, .insertSpaces = true};
  std::optional<std::string> documentUri;
};

std::shared_ptr<workspace::Document>
expectFormatting(TestWorkspace &ws, std::string_view languageId,
                 const ExpectedFormatting &expected);

// ---------------------------------------------------------------------------
// Document & workspace symbols
// ---------------------------------------------------------------------------

struct ExpectedSymbols {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  std::vector<std::string> expectedSymbols;
  std::function<void(const std::vector<::lsp::DocumentSymbol> &)> check;
  std::function<std::string(const ::lsp::DocumentSymbol &)> symbolToString;
};

std::shared_ptr<workspace::Document>
expectSymbols(TestWorkspace &ws, std::string_view languageId,
              const ExpectedSymbols &expected);

struct ExpectedWorkspaceSymbols {
  std::string query;
  std::vector<std::string> expectedSymbols;
  std::function<void(const std::vector<::lsp::WorkspaceSymbol> &)> check;
  std::function<std::string(const ::lsp::WorkspaceSymbol &)> symbolToString;
};

void expectWorkspaceSymbols(TestWorkspace &ws,
                            const ExpectedWorkspaceSymbols &expected);

// ---------------------------------------------------------------------------
// Folding ranges
// ---------------------------------------------------------------------------

struct ExpectedFoldingRanges {
  std::string text;
  std::optional<std::string> documentUri;
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
  std::function<void(const std::vector<::lsp::FoldingRange> &,
                     const std::vector<std::pair<TextOffset, TextOffset>> &)>
      check;
};

std::shared_ptr<workspace::Document>
expectFoldingRanges(TestWorkspace &ws, std::string_view languageId,
                    const ExpectedFoldingRanges &expected);

} // namespace pegium::testing
