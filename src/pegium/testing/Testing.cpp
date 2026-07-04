#include <pegium/testing/Testing.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <type_traits>

#include <gtest/gtest.h>
#include <lsp/types.h>

#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/utils/UriUtils.hpp>
#include <pegium/core/workspace/DocumentBuilder.hpp>
#include <pegium/core/workspace/Documents.hpp>
#include <pegium/core/workspace/TextDocument.hpp>
#include <pegium/core/workspace/WorkspaceManager.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>
#include <pegium/lsp/services/LanguageServerFeatures.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/support/Diagnostics.hpp>
#include <pegium/lsp/workspace/TextDocuments.hpp>
#include <pegium/testing/LspProbe.hpp>

namespace pegium::testing {
namespace {

std::uint32_t g_nextDocumentId = 1;

const pegium::Services *lookup_services(pegium::SharedServices &shared,
                                        std::string_view languageId) {
  for (const auto *coreServices : shared.serviceRegistry->all()) {
    if (coreServices != nullptr &&
        coreServices->languageMetaData.languageId == languageId) {
      return dynamic_cast<const pegium::Services *>(coreServices);
    }
  }
  return nullptr;
}

std::string make_test_uri(const pegium::Services *services) {
  std::string extension = ".test";
  if (services != nullptr &&
      !services->languageMetaData.fileExtensions.empty()) {
    extension = services->languageMetaData.fileExtensions.front();
  }
  return utils::path_to_file_uri("/tmp/pegium-testing/helper-" +
                                 std::to_string(g_nextDocumentId++) + extension);
}

/// Builds a document from an in-memory string through the full pipeline.
std::shared_ptr<workspace::Document>
build_document(pegium::SharedServices &shared, std::string_view languageId,
               std::string text, const std::optional<std::string> &documentUri) {
  const auto *services = lookup_services(shared, languageId);
  if (services == nullptr) {
    ADD_FAILURE() << "No services registered for language " << languageId;
    return nullptr;
  }
  const std::string uri =
      documentUri.has_value() ? *documentUri : make_test_uri(services);

  auto textDocument = std::make_shared<workspace::TextDocument>(
      workspace::TextDocument::create(utils::normalize_uri(uri),
                                      std::string(languageId), 1,
                                      std::move(text)));
  (void)shared.lsp.textDocuments->set(textDocument);

  const auto documentId =
      shared.workspace.documents->getOrCreateDocumentId(textDocument->uri());
  const std::array<workspace::DocumentId, 1> changed{documentId};
  shared.workspace.documentBuilder->update(changed, {});
  return shared.workspace.documents->getDocument(textDocument->uri());
}

} // namespace

// ---------------------------------------------------------------------------
// TestWorkspace
// ---------------------------------------------------------------------------

TestWorkspace::TestWorkspace()
    : TestWorkspace(std::make_unique<pegium::SharedServices>()) {}

TestWorkspace::TestWorkspace(std::unique_ptr<pegium::SharedServices> shared)
    : _shared(std::move(shared)) {
  pegium::installDefaultSharedCoreServices(*_shared);
  pegium::installDefaultSharedLspServices(*_shared);
  _shared->workspace.workspaceManager->initialize(workspace::InitializeParams{});
  auto future =
      _shared->workspace.workspaceManager->initialized(workspace::InitializedParams{});
  if (future.valid()) {
    future.get();
  }
  _shared->workspace.workspaceManager->ready().get();
}

TestWorkspace::~TestWorkspace() = default;
TestWorkspace::TestWorkspace(TestWorkspace &&) noexcept = default;
TestWorkspace &TestWorkspace::operator=(TestWorkspace &&) noexcept = default;

void TestWorkspace::registerLanguage(std::unique_ptr<pegium::Services> services) {
  _shared->serviceRegistry->registerServices(std::move(services));
}

void TestWorkspace::clear() {
  std::vector<workspace::DocumentId> deleted;
  for (const auto &document : _shared->workspace.documents->all()) {
    if (document != nullptr) {
      deleted.push_back(document->id);
    }
  }
  if (!deleted.empty()) {
    _shared->workspace.documentBuilder->update({}, deleted);
  }
}

std::shared_ptr<workspace::Document>
parse(TestWorkspace &ws, std::string_view languageId, std::string text,
      std::optional<std::string> documentUri) {
  return build_document(ws.shared(), languageId, std::move(text), documentUri);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectValidation(TestWorkspace &ws, std::string_view languageId,
                 const ExpectedValidation &expected) {
  const auto marked = replaceIndicesOf(expected);
  auto document =
      build_document(ws.shared(), languageId, marked.output, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  const auto &diagnostics = document->diagnostics;
  if (expected.check) {
    expected.check(diagnostics);
    return document;
  }
  for (const auto &want : expected.diagnostics) {
    const bool matched =
        std::ranges::any_of(diagnostics, [&](const pegium::Diagnostic &got) {
          if (got.message.find(want.message) == std::string::npos) {
            return false;
          }
          if (want.severity.has_value() && got.severity != *want.severity) {
            return false;
          }
          if (want.rangeIndex.has_value()) {
            if (*want.rangeIndex >= marked.ranges.size()) {
              return false;
            }
            const auto [begin, end] = marked.ranges[*want.rangeIndex];
            return got.begin == begin && got.end == end;
          }
          return true;
        });
    EXPECT_TRUE(matched) << "No diagnostic matched expected message: "
                         << want.message;
  }
  EXPECT_EQ(diagnostics.size(), expected.diagnostics.size());
  return document;
}

namespace {

bool matchesFilter(const pegium::Diagnostic &diagnostic,
                   const DiagnosticFilter &filter) {
  if (filter.severity.has_value() && diagnostic.severity != *filter.severity) {
    return false;
  }
  if (filter.code.has_value() &&
      (!diagnostic.code.has_value() || *diagnostic.code != *filter.code)) {
    return false;
  }
  if (filter.offset.has_value() &&
      !(diagnostic.begin <= *filter.offset && *filter.offset <= diagnostic.end)) {
    return false;
  }
  if (filter.range.has_value() &&
      (diagnostic.begin != filter.range->first ||
       diagnostic.end != filter.range->second)) {
    return false;
  }
  if (filter.predicate && !filter.predicate(diagnostic)) {
    return false;
  }
  return true;
}

std::vector<const pegium::Diagnostic *>
filterDiagnostics(const ValidationResult &result,
                  const DiagnosticFilter &filter) {
  std::vector<const pegium::Diagnostic *> matches;
  for (const auto &diagnostic : result.diagnostics) {
    if (matchesFilter(diagnostic, filter)) {
      matches.push_back(&diagnostic);
    }
  }
  return matches;
}

std::string describeFilter(const DiagnosticFilter &filter) {
  std::string parts;
  const auto add = [&parts](std::string_view text) {
    if (!parts.empty()) {
      parts += ", ";
    }
    parts += text;
  };
  if (filter.severity.has_value()) {
    add("severity");
  }
  if (filter.code.has_value()) {
    add("code");
  }
  if (filter.offset.has_value()) {
    add("offset=" + std::to_string(*filter.offset));
  }
  if (filter.range.has_value()) {
    add("range=[" + std::to_string(filter.range->first) + "," +
        std::to_string(filter.range->second) + ")");
  }
  if (filter.predicate) {
    add("predicate");
  }
  return parts.empty() ? std::string("<any>") : parts;
}

void expectWithSeverity(const ValidationResult &result, std::string_view message,
                        DiagnosticFilter filter, DiagnosticSeverity severity) {
  filter.severity = severity;
  const auto matches = filterDiagnostics(result, filter);
  const bool found =
      std::ranges::any_of(matches, [&](const pegium::Diagnostic *diagnostic) {
        return diagnostic->message.find(message) != std::string::npos;
      });
  EXPECT_TRUE(found) << "Expected a diagnostic (" << describeFilter(filter)
                     << ") whose message contains: " << message;
}

} // namespace

ValidationResult validate(TestWorkspace &ws, std::string_view languageId,
                          std::string text,
                          std::optional<std::string> documentUri) {
  ValidationResult result;
  result.document =
      build_document(ws.shared(), languageId, std::move(text), documentUri);
  if (result.document != nullptr) {
    result.diagnostics = result.document->diagnostics;
  }
  return result;
}

void expectNoIssues(const ValidationResult &result,
                    const DiagnosticFilter &filter) {
  const auto matches = filterDiagnostics(result, filter);
  EXPECT_TRUE(matches.empty())
      << "Expected no diagnostics (" << describeFilter(filter) << ") but found "
      << matches.size();
}

void expectIssue(const ValidationResult &result,
                 const DiagnosticFilter &filter) {
  const auto matches = filterDiagnostics(result, filter);
  EXPECT_FALSE(matches.empty())
      << "Expected at least one diagnostic (" << describeFilter(filter) << ")";
}

void expectError(const ValidationResult &result, std::string_view message,
                 const DiagnosticFilter &filter) {
  expectWithSeverity(result, message, filter, DiagnosticSeverity::Error);
}

void expectWarning(const ValidationResult &result, std::string_view message,
                   const DiagnosticFilter &filter) {
  expectWithSeverity(result, message, filter, DiagnosticSeverity::Warning);
}

CodeActionResult testCodeAction(TestWorkspace &ws, std::string_view languageId,
                                std::string input,
                                std::string_view diagnosticCode,
                                std::optional<std::string> outputAfterFix,
                                std::optional<std::string> documentUriArg) {
  CodeActionResult result;
  auto validation =
      validate(ws, languageId, std::move(input), std::move(documentUriArg));
  result.document = validation.document;
  result.diagnosticsAll = validation.diagnostics;
  if (result.document == nullptr) {
    return result;
  }

  std::vector<pegium::Diagnostic> relevant;
  for (const auto &diagnostic : validation.diagnostics) {
    if (diagnostic.code.has_value() &&
        std::holds_alternative<std::string>(*diagnostic.code) &&
        std::get<std::string>(*diagnostic.code) == diagnosticCode) {
      relevant.push_back(diagnostic);
    }
  }
  EXPECT_EQ(relevant.size(), 1u)
      << "Expected exactly one diagnostic with code " << diagnosticCode
      << " but found " << relevant.size();
  if (relevant.size() != 1) {
    return result;
  }
  result.diagnosticRelevant = relevant.front();

  ::lsp::CodeActionParams params{};
  params.textDocument.uri = documentUri(*result.document);
  params.range.start =
      result.document->textDocument().positionAt(relevant.front().begin);
  params.range.end =
      result.document->textDocument().positionAt(relevant.front().end);
  ::lsp::Array<::lsp::Diagnostic> lspDiagnostics;
  lspDiagnostics.reserve(relevant.size());
  for (const auto &diagnostic : relevant) {
    lspDiagnostics.push_back(
        to_lsp_diagnostic(result.document->textDocument(), diagnostic));
  }
  params.context.diagnostics = std::move(lspDiagnostics);

  auto actions = pegium::getCodeActions(ws.shared(), params);

  if (!outputAfterFix.has_value()) {
    return result;
  }

  if (!actions.has_value() || actions->size() != 1) {
    ADD_FAILURE() << "Expected exactly one code action for code "
                  << diagnosticCode;
    return result;
  }
  if (!std::holds_alternative<::lsp::CodeAction>((*actions)[0])) {
    ADD_FAILURE() << "Expected a CodeAction (not a Command)";
    return result;
  }
  const auto &action = std::get<::lsp::CodeAction>((*actions)[0]);
  result.action = action;
  if (!action.edit.has_value() || !action.edit->changes.has_value()) {
    ADD_FAILURE() << "Code action has no workspace edit";
    return result;
  }
  std::vector<::lsp::TextEdit> edits;
  for (const auto &[uri, documentEdits] : *action.edit->changes) {
    edits.insert(edits.end(), documentEdits.begin(), documentEdits.end());
  }
  EXPECT_EQ(applyTextEdits(*result.document, std::move(edits)), *outputAfterFix);
  return result;
}

DecodedSemanticTokens highlight(TestWorkspace &ws, std::string_view languageId,
                                std::string text,
                                std::optional<std::string> documentUriArg) {
  DecodedSemanticTokens result;
  const auto marked = replaceIndices(text);
  result.ranges = marked.ranges;
  auto document = build_document(ws.shared(), languageId, marked.output,
                                 std::move(documentUriArg));
  if (document == nullptr) {
    return result;
  }
  const auto *services = lookup_services(ws.shared(), languageId);
  if (services == nullptr || services->lsp.semanticTokenProvider == nullptr) {
    ADD_FAILURE() << "No semantic-token provider registered for language "
                  << languageId;
    return result;
  }

  const auto typeMap = services->lsp.semanticTokenProvider->tokenTypes();
  std::vector<std::string> legend;
  for (const auto &[name, index] : typeMap) {
    if (index >= legend.size()) {
      legend.resize(index + 1);
    }
    legend[index] = name;
  }

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = documentUri(*document);
  const auto tokens = pegium::getSemanticTokensFull(ws.shared(), params);
  if (tokens.has_value()) {
    result.tokens = decodeSemanticTokens(*tokens, legend, *document);
  }
  return result;
}

void expectSemanticToken(const DecodedSemanticTokens &decoded,
                         const ExpectedSemanticToken &expected) {
  if (expected.rangeIndex >= decoded.ranges.size()) {
    ADD_FAILURE() << "Semantic-token range index " << expected.rangeIndex
                  << " is out of range.";
    return;
  }
  const auto [begin, end] = decoded.ranges[expected.rangeIndex];
  const auto count = std::ranges::count_if(
      decoded.tokens, [&, begin = begin, end = end](
                          const DecodedSemanticToken &token) {
        return token.tokenType == expected.tokenType && token.begin == begin &&
               token.end == end;
      });
  EXPECT_EQ(count, 1) << "Expected exactly one '" << expected.tokenType
                      << "' token covering the marked range";
}

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectCompletion(TestWorkspace &ws, std::string_view languageId,
                 const ExpectedCompletion &expected) {
  auto &shared = ws.shared();
  const auto marked = replaceIndicesOf(expected);
  auto document =
      build_document(shared, languageId, marked.output, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  if (expected.index >= marked.indices.size()) {
    ADD_FAILURE() << "Completion marker index " << expected.index
                  << " is out of range.";
    return document;
  }
  auto completion = pegium::getCompletion(
      shared, completionParams(*document, marked.indices[expected.index]));
  if (!completion.has_value()) {
    ADD_FAILURE() << "No completion result was produced.";
    return document;
  }
  if (expected.check) {
    expected.check(*completion);
    return document;
  }
  auto items = completion->items;
  std::ranges::sort(items, [](const auto &left, const auto &right) {
    return left.sortText.value_or(left.label) <
           right.sortText.value_or(right.label);
  });
  const auto itemToString =
      expected.itemToString ? expected.itemToString
                            : [](const ::lsp::CompletionItem &item) {
                                return item.label;
                              };
  if (items.size() != expected.expectedItems.size()) {
    ADD_FAILURE() << "Expected " << expected.expectedItems.size()
                  << " completion items but found " << items.size();
    return document;
  }
  for (std::size_t index = 0; index < expected.expectedItems.size(); ++index) {
    EXPECT_EQ(itemToString(items[index]), expected.expectedItems[index]);
  }
  return document;
}

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectHover(TestWorkspace &ws, std::string_view languageId,
            const ExpectedHover &expected) {
  auto &shared = ws.shared();
  const auto marked = replaceIndicesOf(expected);
  auto document =
      build_document(shared, languageId, marked.output, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  if (expected.index >= marked.indices.size()) {
    ADD_FAILURE() << "Hover marker index " << expected.index
                  << " is out of range.";
    return document;
  }
  const auto hover = pegium::getHoverContent(
      shared, hoverParams(*document, marked.indices[expected.index]));
  if (expected.check) {
    expected.check(hover);
    return document;
  }
  if (!expected.hover.has_value()) {
    EXPECT_FALSE(hover.has_value());
    return document;
  }
  if (!hover.has_value()) {
    ADD_FAILURE() << "No hover result was produced.";
    return document;
  }
  EXPECT_EQ(hoverText(*hover), *expected.hover);
  return document;
}

// ---------------------------------------------------------------------------
// Go to definition
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectGoToDefinition(TestWorkspace &ws, std::string_view languageId,
                     const ExpectedGoToDefinition &expected) {
  auto &shared = ws.shared();
  const auto marked = replaceIndicesOf(expected);
  auto document =
      build_document(shared, languageId, marked.output, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  if (expected.index >= marked.indices.size()) {
    ADD_FAILURE() << "Definition marker index " << expected.index
                  << " is out of range.";
    return document;
  }
  auto links = pegium::getDefinition(
      shared, definitionParams(*document, marked.indices[expected.index]));
  if (!links.has_value()) {
    ADD_FAILURE() << "No definition result was produced.";
    return document;
  }
  const auto expectedRanges =
      resolveRangeIndices(expected.rangeIndex, marked.ranges.size());
  if (links->size() != expectedRanges.size()) {
    ADD_FAILURE() << "Expected " << expectedRanges.size()
                  << " definitions but found " << links->size();
    return document;
  }
  for (std::size_t index = 0; index < expectedRanges.size(); ++index) {
    const auto [begin, end] = marked.ranges[expectedRanges[index]];
    EXPECT_TRUE(samePosition((*links)[index].targetSelectionRange.start,
                             document->textDocument().positionAt(begin)));
    EXPECT_TRUE(samePosition((*links)[index].targetSelectionRange.end,
                             document->textDocument().positionAt(end)));
  }
  return document;
}

// ---------------------------------------------------------------------------
// Find references
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectFindReferences(TestWorkspace &ws, std::string_view languageId,
                     const ExpectedFindReferences &expected) {
  auto &shared = ws.shared();
  const auto marked = replaceIndicesOf(expected);
  auto document =
      build_document(shared, languageId, marked.output, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  if (marked.indices.empty()) {
    // No cursor marker means the assertion loop below never runs; fail loudly
    // instead of silently passing while testing nothing.
    ADD_FAILURE() << "expectFindReferences: source has no '"
                  << expected.indexMarker << "' cursor marker.";
    return document;
  }
  for (const auto offset : marked.indices) {
    const auto references = pegium::getReferences(
        shared, referenceParams(*document, offset, expected.includeDeclaration));
    if (references.size() != marked.ranges.size()) {
      ADD_FAILURE() << "Expected " << marked.ranges.size()
                    << " references but found " << references.size();
      return document;
    }
    for (const auto &reference : references) {
      const auto match =
          std::ranges::any_of(marked.ranges, [&](const auto &expectedRange) {
            return samePosition(reference.range.start,
                                document->textDocument().positionAt(
                                    expectedRange.first)) &&
                   samePosition(reference.range.end,
                                document->textDocument().positionAt(
                                    expectedRange.second));
          });
      EXPECT_TRUE(match);
    }
  }
  return document;
}

// ---------------------------------------------------------------------------
// Document highlight
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectHighlight(TestWorkspace &ws, std::string_view languageId,
                const ExpectedHighlight &expected) {
  auto &shared = ws.shared();
  const auto marked = replaceIndicesOf(expected);
  auto document =
      build_document(shared, languageId, marked.output, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  if (expected.index >= marked.indices.size()) {
    ADD_FAILURE() << "Highlight marker index " << expected.index
                  << " is out of range.";
    return document;
  }
  const auto highlights = pegium::getDocumentHighlights(
      shared, documentHighlightParams(*document, marked.indices[expected.index]));
  const auto expectedRanges =
      resolveRangeIndices(expected.rangeIndex, marked.ranges.size());
  if (highlights.size() != expectedRanges.size()) {
    ADD_FAILURE() << "Expected " << expectedRanges.size()
                  << " highlights but found " << highlights.size();
    return document;
  }
  for (std::size_t index = 0; index < expectedRanges.size(); ++index) {
    const auto [begin, end] = marked.ranges[expectedRanges[index]];
    EXPECT_TRUE(samePosition(highlights[index].range.start,
                             document->textDocument().positionAt(begin)));
    EXPECT_TRUE(samePosition(highlights[index].range.end,
                             document->textDocument().positionAt(end)));
  }
  return document;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectFormatting(TestWorkspace &ws, std::string_view languageId,
                 const ExpectedFormatting &expected) {
  auto &shared = ws.shared();
  auto document =
      build_document(shared, languageId, expected.before, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  std::vector<::lsp::TextEdit> edits;
  if (expected.range.has_value()) {
    edits = pegium::formatDocumentRange(
        shared, ::lsp::DocumentRangeFormattingParams{
                    .textDocument = {.uri = documentUri(*document)},
                    .range = *expected.range,
                    .options = expected.options});
  } else {
    edits = pegium::formatDocument(
        shared, ::lsp::DocumentFormattingParams{
                    .textDocument = {.uri = documentUri(*document)},
                    .options = expected.options});
  }
  EXPECT_EQ(applyTextEdits(*document, std::move(edits)), expected.after);
  return document;
}

// ---------------------------------------------------------------------------
// Symbols
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectSymbols(TestWorkspace &ws, std::string_view languageId,
              const ExpectedSymbols &expected) {
  auto &shared = ws.shared();
  auto document =
      build_document(shared, languageId, expected.text, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  const auto symbols = pegium::getDocumentSymbols(
      shared, ::lsp::DocumentSymbolParams{
                  .textDocument = {.uri = documentUri(*document)}});
  if (expected.check) {
    expected.check(symbols);
    return document;
  }
  const auto symbolToString =
      expected.symbolToString ? expected.symbolToString
                              : [](const ::lsp::DocumentSymbol &symbol) {
                                  return symbol.name;
                                };
  if (symbols.size() != expected.expectedSymbols.size()) {
    ADD_FAILURE() << "Expected " << expected.expectedSymbols.size()
                  << " document symbols but found " << symbols.size();
    return document;
  }
  for (std::size_t index = 0; index < expected.expectedSymbols.size(); ++index) {
    EXPECT_EQ(symbolToString(symbols[index]), expected.expectedSymbols[index]);
  }
  return document;
}

void expectWorkspaceSymbols(TestWorkspace &ws,
                            const ExpectedWorkspaceSymbols &expected) {
  const auto symbols = pegium::getWorkspaceSymbols(
      ws.shared(), ::lsp::WorkspaceSymbolParams{.query = expected.query});
  if (expected.check) {
    expected.check(symbols);
    return;
  }
  const auto symbolToString =
      expected.symbolToString ? expected.symbolToString
                              : [](const ::lsp::WorkspaceSymbol &symbol) {
                                  return symbol.name;
                                };
  if (symbols.size() != expected.expectedSymbols.size()) {
    ADD_FAILURE() << "Expected " << expected.expectedSymbols.size()
                  << " workspace symbols but found " << symbols.size();
    return;
  }
  for (std::size_t index = 0; index < expected.expectedSymbols.size(); ++index) {
    EXPECT_EQ(symbolToString(symbols[index]), expected.expectedSymbols[index]);
  }
}

// ---------------------------------------------------------------------------
// Folding ranges
// ---------------------------------------------------------------------------

std::shared_ptr<workspace::Document>
expectFoldingRanges(TestWorkspace &ws, std::string_view languageId,
                    const ExpectedFoldingRanges &expected) {
  auto &shared = ws.shared();
  const auto marked = replaceIndicesOf(expected);
  auto document =
      build_document(shared, languageId, marked.output, expected.documentUri);
  if (document == nullptr) {
    return nullptr;
  }
  auto ranges = pegium::getFoldingRanges(
      shared, ::lsp::FoldingRangeParams{
                  .textDocument = {.uri = documentUri(*document)}});
  std::ranges::sort(ranges, [](const auto &left, const auto &right) {
    return left.startLine < right.startLine;
  });
  if (expected.check) {
    expected.check(ranges, marked.ranges);
    return document;
  }
  if (ranges.size() != marked.ranges.size()) {
    ADD_FAILURE() << "Expected " << marked.ranges.size()
                  << " folding ranges but found " << ranges.size();
    return document;
  }
  for (std::size_t index = 0; index < marked.ranges.size(); ++index) {
    const auto [begin, end] = marked.ranges[index];
    EXPECT_EQ(ranges[index].startLine,
              document->textDocument().positionAt(begin).line);
    EXPECT_EQ(ranges[index].endLine,
              document->textDocument().positionAt(end).line);
  }
  return document;
}

} // namespace pegium::testing
