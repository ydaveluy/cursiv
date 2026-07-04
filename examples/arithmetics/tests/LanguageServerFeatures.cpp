#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <arithmetics/lsp/LspModule.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/lsp/LspExpectTestSupport.hpp>
#include <pegium/lsp/navigation/DefaultReferencesProvider.hpp>
#include <pegium/lsp/services/LanguageServerFeatures.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>

namespace pegium::integration {
namespace {
bool is_parse_diagnostic(const pegium::Diagnostic &diagnostic) {
  if (diagnostic.source == "parse") {
    return true;
  }
  if (!diagnostic.code.has_value()) {
    return false;
  }
  const auto *code = std::get_if<std::string>(&*diagnostic.code);
  return code != nullptr && code->starts_with("parse.");
}

std::optional<::lsp::TextEdit>
completion_text_edit(const ::lsp::CompletionItem &item) {
  if (!item.textEdit.has_value()) {
    return std::nullopt;
  }
  return std::get_if<::lsp::TextEdit>(&*item.textEdit) != nullptr
             ? std::optional<::lsp::TextEdit>{
                   *std::get_if<::lsp::TextEdit>(&*item.textEdit)}
             : std::nullopt;
}

std::string snippet_body(std::string_view text) {
  std::string expanded;
  expanded.reserve(text.size());

  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '$' && index + 1 < text.size() && text[index + 1] == '{') {
      const auto close = text.find('}', index + 2);
      if (close == std::string_view::npos) {
        expanded.push_back(text[index++]);
        continue;
      }
      const auto colon = text.find(':', index + 2);
      if (colon != std::string_view::npos && colon < close) {
        expanded.append(text.substr(colon + 1, close - colon - 1));
      }
      index = close + 1;
      continue;
    }
    expanded.push_back(text[index++]);
  }

  return expanded;
}

std::vector<std::string>
completion_labels(const ::lsp::CompletionList &completion) {
  std::vector<std::string> labels;
  labels.reserve(completion.items.size());
  for (const auto &item : completion.items) {
    labels.push_back(item.label);
  }
  return labels;
}

struct CompletionProbe {
  std::shared_ptr<workspace::Document> document;
  TextOffset offset = 0;
  ::lsp::CompletionList completion;
};

CompletionProbe completion_probe(pegium::SharedServices &shared,
                                 std::string_view languageId,
                                 std::string textWithMarker) {
  const pegium::test::ExpectedCompletion expected{.text = std::move(textWithMarker)};
  const auto marked = test::replaceIndices(expected);
  EXPECT_EQ(marked.indices.size(), 1u);

  auto document = test::parseDocument(shared, languageId, marked.output);
  if (document == nullptr || marked.indices.empty()) {
    return {};
  }

  auto completion =
      pegium::getCompletion(shared,
                                 test::completionParams(*document, marked.indices[0]));
  EXPECT_TRUE(completion.has_value());
  if (!completion.has_value()) {
    return {.document = std::move(document), .offset = marked.indices[0]};
  }

  return {.document = std::move(document),
          .offset = marked.indices[0],
          .completion = std::move(*completion)};
}

std::string apply_completion_choice(
    const CompletionProbe &probe, std::string_view label,
    std::string_view snippetReplacement = {}) {
  const auto itemIt = std::ranges::find(
      probe.completion.items, label, &::lsp::CompletionItem::label);
  const auto labels = completion_labels(probe.completion);
  if (itemIt == probe.completion.items.end()) {
    ADD_FAILURE() << "Expected completion item " << label
                  << " in " << ::testing::PrintToString(labels);
    auto text = probe.document != nullptr
                    ? std::string(probe.document->textDocument().getText())
                    : std::string{};
    text.insert(std::min<std::size_t>(probe.offset, text.size()), "<|>");
    return text;
  }

  const auto edit = completion_text_edit(*itemIt);
  if (!edit.has_value()) {
    ADD_FAILURE() << "Completion item " << label << " has no text edit";
    auto text = probe.document != nullptr
                    ? std::string(probe.document->textDocument().getText())
                    : std::string{};
    text.insert(std::min<std::size_t>(probe.offset, text.size()), "<|>");
    return text;
  }

  auto replacement = snippet_body(edit->newText);
  if (!snippetReplacement.empty()) {
    replacement = std::string(snippetReplacement);
  }

  const auto &textDocument = probe.document->textDocument();
  auto text = std::string(textDocument.getText());
  const auto begin = textDocument.offsetAt(edit->range.start);
  const auto end = textDocument.offsetAt(edit->range.end);
  text.replace(begin, end - begin, replacement);
  text.insert(begin + replacement.size(), "<|>");
  return text;
}

class LanguageServerFeaturesIntegrationTest : public ::testing::Test {
protected:
  std::unique_ptr<arithmetics::ArithmeticsSharedServices> shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();

  LanguageServerFeaturesIntegrationTest() {
    pegium::installDefaultSharedCoreServices(*shared);
    pegium::installDefaultSharedLspServices(*shared);
    pegium::test::initialize_shared_workspace_for_tests(*shared);
  }

  void SetUp() override {
    ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));
  }

  std::shared_ptr<workspace::Document> openArithmeticsDocument(
      std::string_view fileName, std::string text) {
    return test::open_and_build_document(*shared, test::make_file_uri(fileName),
                                         "arithmetics", std::move(text));
  }

  std::shared_ptr<workspace::Document>
  parseArithmeticsDocument(std::string text) {
    return test::parseDocument(*shared, "arithmetics", std::move(text));
  }

  template <typename Check>
  void expectArithmeticsCompletion(std::string text, Check &&check) {
    auto document = test::expectCompletion(
        *shared, "arithmetics",
        pegium::test::ExpectedCompletion{.text = std::move(text),
                                         .check = std::forward<Check>(check)});
    ASSERT_NE(document, nullptr);
  }

  CompletionProbe probeCompletion(std::string text) {
    return completion_probe(*shared, "arithmetics", std::move(text));
  }

  const pegium::Services *arithmeticsServices() const {
    const auto *coreServices =
        &shared->serviceRegistry->getServices(test::make_file_uri("lookup.calc"));
    return dynamic_cast<const pegium::Services *>(coreServices);
  }

  static std::string completionLabelDump(const ::lsp::CompletionList &completion) {
    return ::testing::PrintToString(completion_labels(completion));
  }

  static void expectHasCompletionLabel(const ::lsp::CompletionList &completion,
                                       std::string_view label) {
    EXPECT_TRUE(std::ranges::any_of(
        completion.items, [label](const auto &item) { return item.label == label; }))
        << completionLabelDump(completion);
  }

  static void expectMissingCompletionLabel(const ::lsp::CompletionList &completion,
                                           std::string_view label) {
    EXPECT_FALSE(std::ranges::any_of(
        completion.items, [label](const auto &item) { return item.label == label; }))
        << completionLabelDump(completion);
  }

  static void expectNoParseDiagnostics(const workspace::Document &document) {
    EXPECT_TRUE(std::ranges::none_of(
        document.diagnostics, [](const auto &diagnostic) {
          return is_parse_diagnostic(diagnostic);
        }));
  }
};

TEST_F(LanguageServerFeaturesIntegrationTest,
       DefinitionResolvesReferenceLocation) {
  auto document = openArithmeticsDocument(
      "definition.calc",
      "module Demo\n"
      "def value: 1;\n"
      "value;\n");
  ASSERT_NE(document, nullptr);
  ASSERT_EQ(document->state, pegium::workspace::DocumentState::Validated);

  ::lsp::DefinitionParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.position.line = 2;
  params.position.character = 1;

  const auto locations = pegium::getDefinition(*shared, params);
  ASSERT_TRUE(locations.has_value());
  ASSERT_EQ(locations->size(), 1u);
  EXPECT_EQ(locations->front().targetUri.toString(), document->uri);
  EXPECT_EQ(locations->front().targetSelectionRange.start.line, 1u);
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       ReferencesIncludeDeclarationAndUsage) {
  auto document = openArithmeticsDocument(
      "references.calc",
      "module Demo\n"
      "def value: 1;\n"
      "value;\n");
  ASSERT_NE(document, nullptr);

  ::lsp::ReferenceParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.position.line = 2;
  params.position.character = 1;
  params.context.includeDeclaration = true;

  const auto references = pegium::getReferences(*shared, params);
  EXPECT_EQ(references.size(), 2u);
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       PrepareRenameReturnsTokenRangeForNamedReference) {
  auto document = openArithmeticsDocument(
      "prepare-rename.calc",
      "module Demo\n"
      "def value: 1;\n"
      "value;\n");
  ASSERT_NE(document, nullptr);

  ::lsp::PrepareRenameParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.position.line = 2;
  params.position.character = 2;

  const auto range = pegium::prepareRename(*shared, params);
  ASSERT_TRUE(range.has_value());
  ASSERT_TRUE(std::holds_alternative<::lsp::Range>(*range));
  const auto &renameRange = std::get<::lsp::Range>(*range);
  EXPECT_EQ(renameRange.start.line, 2u);
  EXPECT_EQ(renameRange.start.character, 0u);
  EXPECT_EQ(renameRange.end.line, 2u);
  EXPECT_EQ(renameRange.end.character, 5u);
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       RenameReturnsWorkspaceEditForDeclarationAndReference) {
  auto document = openArithmeticsDocument(
      "rename.calc",
      "module Demo\n"
      "def value: 1;\n"
      "value;\n");
  ASSERT_NE(document, nullptr);

  ::lsp::RenameParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.position.line = 2;
  params.position.character = 2;
  params.newName = "renamed";

  const auto edit = pegium::rename(*shared, params);
  ASSERT_TRUE(edit.has_value());
  ASSERT_TRUE(edit->changes.has_value());
  const auto changeIt =
      edit->changes->find(::lsp::DocumentUri(::lsp::Uri::parse(document->uri)));
  ASSERT_NE(changeIt, edit->changes->end());

  const auto &edits = changeIt->second;
  ASSERT_EQ(edits.size(), 2u);
  EXPECT_EQ(edits[0].newText, "renamed");
  EXPECT_EQ(edits[1].newText, "renamed");
  EXPECT_EQ(edits[0].range.start.line, 2u);
  EXPECT_EQ(edits[1].range.start.line, 1u);
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       WorkspaceSymbolsReturnIndexedDeclarations) {
  auto document = openArithmeticsDocument(
      "workspace-symbols.calc",
      "module Demo\n"
      "def value: 1;\n"
      "def other: value;\n");
  ASSERT_NE(document, nullptr);

  ::lsp::WorkspaceSymbolParams params{};
  params.query = "val";

  const auto symbols = pegium::getWorkspaceSymbols(*shared, params);
  ASSERT_FALSE(symbols.empty());
  EXPECT_TRUE(std::ranges::any_of(symbols, [&document](const auto &symbol) {
    if (symbol.name != "value") {
      return false;
    }
    return std::visit(
        [&document](const auto &location) {
          using Location = std::decay_t<decltype(location)>;
          if constexpr (std::is_same_v<Location, ::lsp::Location>) {
            return location.uri.toString() == document->uri &&
                   location.range.start.line == 1u &&
                   location.range.start.character == 4u &&
                   location.range.end.line == 1u &&
                   location.range.end.character == 9u;
          } else {
            return false;
          }
        },
        symbol.location);
  }));
}

// --- Regression: index entries can outlive their document in the store ---
//
// The document builder removes a deleted document from the store
// (deleteDocument) before it drops the document's contributions from the index
// (cleanUpDeleted). A concurrent reader that snapshots the index in that window
// observes an entry whose documentId no longer resolves to a stored document.
// These three tests reproduce that window deterministically (delete from the
// store, leave the index intact) and assert the LSP providers skip the stale
// entry instead of dereferencing a null document.

TEST_F(LanguageServerFeaturesIntegrationTest,
       WorkspaceSymbolsSkipDocumentDeletedFromStore) {
  auto document = openArithmeticsDocument(
      "stale-workspace-symbols.calc",
      "module Demo\n"
      "def value: 1;\n");
  ASSERT_NE(document, nullptr);

  ::lsp::WorkspaceSymbolParams params{};
  params.query = "val";

  // Pre-deletion: the index advertises `value`, so the post-deletion check below
  // is known to reflect the guard skipping a still-indexed entry (not a vacuous
  // empty result).
  const auto before = pegium::getWorkspaceSymbols(*shared, params);
  ASSERT_TRUE(std::ranges::any_of(
      before, [](const auto &symbol) { return symbol.name == "value"; }));

  const auto documentId = document->id;
  const auto removed = shared->workspace.documents->deleteDocument(documentId);
  ASSERT_NE(removed, nullptr);
  ASSERT_EQ(shared->workspace.documents->getDocument(documentId), nullptr);

  const auto symbols = pegium::getWorkspaceSymbols(*shared, params);
  EXPECT_TRUE(std::ranges::none_of(symbols, [](const auto &symbol) {
    return symbol.name == "value";
  }));
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       ReferencesSkipDocumentDeletedFromStore) {
  auto document = openArithmeticsDocument(
      "stale-references.calc",
      "module Demo\n"
      "def value: 1;\n"
      "value;\n");
  ASSERT_NE(document, nullptr);

  const auto *services = arithmeticsServices();
  ASSERT_NE(services, nullptr);

  ::lsp::ReferenceParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.position.line = 2;
  params.position.character = 1;
  params.context.includeDeclaration = true;

  const auto documentId = document->id;
  const auto removed = shared->workspace.documents->deleteDocument(documentId);
  ASSERT_NE(removed, nullptr);
  ASSERT_EQ(shared->workspace.documents->getDocument(documentId), nullptr);

  const pegium::DefaultReferencesProvider provider{*services};
  const auto locations =
      provider.findReferences(*document, params, utils::default_cancel_token);
  EXPECT_TRUE(locations.empty());
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       WorkspaceEditSkipsDocumentDeletedFromStore) {
  auto document = openArithmeticsDocument(
      "stale-rename.calc",
      "module Demo\n"
      "def value: 1;\n");
  ASSERT_NE(document, nullptr);

  const auto documentId = document->id;
  const auto removed = shared->workspace.documents->deleteDocument(documentId);
  ASSERT_NE(removed, nullptr);
  ASSERT_EQ(shared->workspace.documents->getDocument(documentId), nullptr);

  pegium::provider_detail::WorkspaceEditData editData{};
  editData.changes[documentId].push_back(
      pegium::provider_detail::WorkspaceTextEdit{
          .begin = 16, .end = 21, .newText = "renamed"});

  const auto edit = pegium::provider_detail::to_lsp_workspace_edit(
      editData, *shared, utils::default_cancel_token);
  EXPECT_FALSE(edit.has_value());
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       FoldingRangesOnEmptyArithmeticDocumentStayEmpty) {
  auto document = openArithmeticsDocument("empty-folding.calc", "");
  ASSERT_NE(document, nullptr);

  ::lsp::FoldingRangeParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto ranges = pegium::getFoldingRanges(*shared, params);
  EXPECT_TRUE(ranges.empty());
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionAfterModuleHeaderProposesDefinitionKeyword) {
  expectArithmeticsCompletion(
      "module name\n"
      "<|>",
      [](const ::lsp::CompletionList &completion) {
        EXPECT_TRUE(std::ranges::any_of(
            completion.items,
            [](const auto &item) { return item.label == "def"; }));
      });
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionInsideBlankLineBeforeExistingStatementProposesDefinitionKeyword) {
  expectArithmeticsCompletion(
      "module mod\n"
      "<|>\n"
      "def ID : 0 ;",
      [](const ::lsp::CompletionList &completion) {
        EXPECT_TRUE(std::ranges::any_of(
            completion.items,
            [](const auto &item) { return item.label == "def"; }));
      });
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionInsideDefinitionAfterNameProposesColon) {
  expectArithmeticsCompletion(
      "module name\n"
      "def a <|>",
      [](const ::lsp::CompletionList &completion) {
        EXPECT_TRUE(std::ranges::any_of(
            completion.items, [](const auto &item) { return item.label == ":"; }));
        EXPECT_FALSE(std::ranges::any_of(
            completion.items, [](const auto &item) { return item.label == ";"; }));
        EXPECT_FALSE(std::ranges::any_of(
            completion.items, [](const auto &item) { return item.label == "def"; }));
      });
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionInsideDefinitionAfterKeywordProposesIdentifierRule) {
  expectArithmeticsCompletion(
      "module name\n"
      "def <|>",
      [](const ::lsp::CompletionList &completion) {
        EXPECT_TRUE(std::ranges::any_of(
            completion.items, [](const auto &item) { return item.label == "ID"; }));
        EXPECT_FALSE(std::ranges::any_of(
            completion.items, [](const auto &item) { return item.label == ":"; }));
      });
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionInsideDefinitionAfterColonProposesExpressionNotSemicolon) {
  expectArithmeticsCompletion(
      "module name\n"
      "def a : <|>",
      [](const ::lsp::CompletionList &completion) {
        const auto dump = completionLabelDump(completion);
        EXPECT_TRUE(std::ranges::any_of(completion.items, [](const auto &item) {
          return item.label == "NUMBER";
        })) << dump;
        EXPECT_FALSE(std::ranges::any_of(completion.items, [](const auto &item) {
          return item.label == ";";
        })) << dump;
      });
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionCanReconstructDefinitionFromEmptyDocument) {
  auto text = std::string{"<|>"};

  auto probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, "module");
  text = apply_completion_choice(probe, "module");
  text.insert(text.find("<|>"), " ");

  probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, "ID");
  text = apply_completion_choice(probe, "ID", "mod");
  text.insert(text.find("<|>"), "\n");

  probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, "def");
  text = apply_completion_choice(probe, "def");
  text.insert(text.find("<|>"), " ");

  probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, "ID");
  text = apply_completion_choice(probe, "ID", "a");
  text.insert(text.find("<|>"), " ");

  probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, ":");
  text = apply_completion_choice(probe, ":");
  text.insert(text.find("<|>"), " ");

  probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, "NUMBER");
  expectMissingCompletionLabel(probe.completion, ";");
  text = apply_completion_choice(probe, "NUMBER", "0");

  probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, ";");
  text = apply_completion_choice(probe, ";");

  const auto finalDocument =
      parseArithmeticsDocument(text.substr(0, text.find("<|>")));
  ASSERT_NE(finalDocument, nullptr);
  EXPECT_TRUE(finalDocument->parseResult.fullMatch);
  EXPECT_FALSE(finalDocument->parseRecovered());
  expectNoParseDiagnostics(*finalDocument);
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionCanContinueFromDefinitionToEvaluation) {
  auto text =
      std::string{"module mod\n"
                  "def a : 0 ;\n"
                  "<|>"};

  auto probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, "def");
  expectHasCompletionLabel(probe.completion, "NUMBER");

  text = apply_completion_choice(probe, "NUMBER", "3");

  probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, ";");
  text = apply_completion_choice(probe, ";");

  const auto finalDocument =
      parseArithmeticsDocument(text.substr(0, text.find("<|>")));
  ASSERT_NE(finalDocument, nullptr);
  EXPECT_TRUE(finalDocument->parseResult.fullMatch);
  EXPECT_FALSE(finalDocument->parseRecovered());
  expectNoParseDiagnostics(*finalDocument);
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionContinuesAfterLongEvaluationStatement) {
  const auto text =
      std::string{"module mod\n"
                  "def a: 0;\n"
                  "2 * 8 / 7 - 9 + 34848 * 654296 / 49 - 9262 + 9626;\n"
                  "<|>"};

  const auto marked = test::replaceIndices(
      pegium::test::ExpectedCompletion{.text = text});
  const auto *services = arithmeticsServices();
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->parser, nullptr);
  const auto expect =
      services->parser->expect(marked.output, marked.indices.front());
  EXPECT_FALSE(expect.frontier.empty());

  const auto probe = probeCompletion(text);
  expectHasCompletionLabel(probe.completion, "def");
  expectHasCompletionLabel(probe.completion, "NUMBER");
}

TEST_F(LanguageServerFeaturesIntegrationTest,
       CompletionReplacesWholeReferenceTokenNotJustUpToCursor) {
  auto document = openArithmeticsDocument(
      "completion-mid-reference.calc",
      "module Demo\n"
      "def value: 1;\n"
      "value;\n");
  ASSERT_NE(document, nullptr);

  const auto &textDocument = document->textDocument();
  // Cursor in the middle of the `value` usage on line 2 (va|lue).
  ::lsp::Position position{};
  position.line = 2;
  position.character = 2;
  const auto offset = textDocument.offsetAt(position);

  const auto completion =
      pegium::getCompletion(*shared, test::completionParams(*document, offset));
  ASSERT_TRUE(completion.has_value());

  // The replaced range must cover the whole `value` token [2:0, 2:5), not stop
  // at the cursor (character 2) — otherwise accepting a candidate leaves the
  // "lue" suffix behind.
  bool sawEdit = false;
  for (const auto &item : completion->items) {
    const auto edit = completion_text_edit(item);
    if (!edit.has_value()) {
      continue;
    }
    sawEdit = true;
    EXPECT_EQ(edit->range.start.line, 2u);
    EXPECT_EQ(edit->range.start.character, 0u);
    EXPECT_EQ(edit->range.end.line, 2u);
    EXPECT_EQ(edit->range.end.character, 5u);
  }
  EXPECT_TRUE(sawEdit);
}

} // namespace
} // namespace pegium::integration
