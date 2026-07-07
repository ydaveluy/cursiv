#include <gtest/gtest.h>

#include <memory>

#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#include <pegium/core/workspace/TextDocumentProvider.hpp>
#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/lsp/support/Diagnostics.hpp>

namespace pegium {
namespace {

class SingleDocumentProvider final : public workspace::TextDocumentProvider {
public:
  explicit SingleDocumentProvider(std::shared_ptr<workspace::TextDocument> doc)
      : _doc(std::move(doc)) {}

  [[nodiscard]] std::shared_ptr<workspace::TextDocument>
  getNormalized(std::string_view normalizedUri) const override {
    return (_doc != nullptr && _doc->uri() == normalizedUri) ? _doc : nullptr;
  }

private:
  std::shared_ptr<workspace::TextDocument> _doc;
};

TEST(DiagnosticsTest, PublishDiagnosticsSerializesExtendedDiagnosticFields) {
  test::MemoryStream stream;
  ::lsp::Connection connection(stream);
  ::lsp::MessageHandler handler(connection);

  workspace::DocumentDiagnosticsSnapshot snapshot{
      .uri = test::make_file_uri("diagnostics.test"),
      .text = "alpha\nbeta\n",
      .version = 7,
      .diagnostics =
          {{
              .severity = pegium::DiagnosticSeverity::Warning,
              .message = "Use a better name",
              .source = "pegium-test",
              .code = std::int64_t{std::numeric_limits<std::int64_t>::max()},
              .codeDescription = "https://example.test/diagnostic",
              .tags = {
                  pegium::DiagnosticTag::Deprecated,
                  pegium::DiagnosticTag::Unnecessary,
              },
              .relatedInformation =
                  {{
                      .uri = {},
                      .message = "Related entry",
                      .begin = 0,
                      .end = 5,
                  }},
              .data = pegium::JsonValue::Object{
                  {"category", "style"},
                  {"fixable", true},
              },
              .begin = 6,
              .end = 10,
          }},
  };

  publish_diagnostics(&handler, snapshot);

  const auto message = test::parse_last_written_message(stream.written()).object();
  EXPECT_EQ(message.get("method").string(),
            ::lsp::notifications::TextDocument_PublishDiagnostics::Method);

  const auto &params = message.get("params").object();
  EXPECT_EQ(params.get("uri").string(), snapshot.uri);
  ASSERT_TRUE(params.contains("version"));
  EXPECT_EQ(params.get("version").integer(), 7);

  const auto &diagnostics = params.get("diagnostics").array();
  ASSERT_EQ(diagnostics.size(), 1u);
  const auto &diagnostic = diagnostics.front().object();

  EXPECT_EQ(diagnostic.get("message").string(), "Use a better name");
  EXPECT_EQ(diagnostic.get("source").string(), "pegium-test");
  EXPECT_EQ(diagnostic.get("code").integer(),
            std::numeric_limits<::lsp::json::Integer>::max());

  const auto &codeDescription = diagnostic.get("codeDescription").object();
  EXPECT_EQ(codeDescription.get("href").string(),
            "https://example.test/diagnostic");

  const auto &tags = diagnostic.get("tags").array();
  ASSERT_EQ(tags.size(), 2u);
  EXPECT_EQ(tags[0].integer(), 2);
  EXPECT_EQ(tags[1].integer(), 1);

  const auto &relatedInformation =
      diagnostic.get("relatedInformation").array();
  ASSERT_EQ(relatedInformation.size(), 1u);
  const auto &relatedEntry = relatedInformation.front().object();
  EXPECT_EQ(relatedEntry.get("message").string(), "Related entry");
  EXPECT_EQ(
      relatedEntry.get("location").object().get("uri").string(),
      snapshot.uri);

  const auto &data = diagnostic.get("data").object();
  EXPECT_EQ(data.get("category").string(), "style");
  EXPECT_TRUE(data.get("fixable").boolean());

  const auto &range = diagnostic.get("range").object();
  EXPECT_EQ(range.get("start").object().get("line").integer(), 1);
  EXPECT_EQ(range.get("start").object().get("character").integer(), 0);
  EXPECT_EQ(range.get("end").object().get("character").integer(), 4);
}

TEST(DiagnosticsTest, PublishDiagnosticsSkipsInvalidCodeDescriptionUri) {
  test::MemoryStream stream;
  ::lsp::Connection connection(stream);
  ::lsp::MessageHandler handler(connection);

  workspace::DocumentDiagnosticsSnapshot snapshot{
      .uri = test::make_file_uri("diagnostics-invalid-uri.test"),
      .text = "alpha",
      .diagnostics =
          {{
              .message = "bad uri",
              .codeDescription = "not a uri",
          }},
  };

  publish_diagnostics(&handler, snapshot);

  const auto message = test::parse_last_written_message(stream.written()).object();
  EXPECT_FALSE(message.get("params").object().contains("version"));
  const auto &diagnostics =
      message.get("params").object().get("diagnostics").array();
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_FALSE(diagnostics.front().object().contains("codeDescription"));
}

TEST(DiagnosticsTest, RelatedInformationRangeUsesCrossFileTargetDocument) {
  const auto uriA = test::make_file_uri("diag-a.test");
  const auto uriB = test::make_file_uri("diag-b.test");
  auto docA = workspace::TextDocument::create(uriA, "", 1, "alpha\nbeta\n");
  auto docB = std::make_shared<workspace::TextDocument>(
      workspace::TextDocument::create(uriB, "", 1, "x\ny\nz\nWORD\n"));
  SingleDocumentProvider provider(docB);

  Diagnostic diagnostic{
      .severity = pegium::DiagnosticSeverity::Error,
      .message = "cross-file",
      .relatedInformation = {{
          .uri = uriB,
          .message = "see other file",
          .begin = 6,
          .end = 10,
      }},
      .begin = 6,
      .end = 10,
  };

  const auto lsp = to_lsp_diagnostic(docA, diagnostic, &provider);
  ASSERT_TRUE(lsp.relatedInformation.has_value());
  ASSERT_EQ(lsp.relatedInformation->size(), 1u);
  // Offset 6 is line 3 in docB ("x\\ny\\nz\\nWORD") but line 1 in docA: the range
  // must be computed against the target document named by the entry.
  EXPECT_EQ(lsp.relatedInformation->front().location.range.start.line, 3u);
  // The diagnostic's own range still maps against docA (offset 6 -> line 1).
  EXPECT_EQ(lsp.range.start.line, 1u);
}

TEST(DiagnosticsTest,
     RelatedInformationRangeIsZeroWhenNoCrossFileProviderIsGiven) {
  const auto uriA = test::make_file_uri("diag-a.test");
  const auto uriB = test::make_file_uri("diag-b.test");
  auto docA = workspace::TextDocument::create(uriA, "", 1, "alpha\nbeta\n");

  Diagnostic diagnostic{
      .severity = pegium::DiagnosticSeverity::Error,
      .message = "cross-file",
      .relatedInformation = {{
          .uri = uriB,
          .message = "see other file",
          .begin = 6,
          .end = 10,
      }},
      .begin = 6,
      .end = 10,
  };

  // No crossFileProvider at all: reusing docA's offsets for a docB-relative
  // entry would silently produce a wrong-but-plausible position, so the range
  // must be {0,0} instead.
  const auto lsp = to_lsp_diagnostic(docA, diagnostic, nullptr);
  ASSERT_TRUE(lsp.relatedInformation.has_value());
  ASSERT_EQ(lsp.relatedInformation->size(), 1u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.start.line, 0u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.start.character, 0u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.end.line, 0u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.end.character, 0u);
}

TEST(DiagnosticsTest,
     RelatedInformationRangeIsZeroWhenCrossFileProviderCannotResolveIt) {
  const auto uriA = test::make_file_uri("diag-a.test");
  const auto uriB = test::make_file_uri("diag-b.test");
  const auto uriC = test::make_file_uri("diag-c.test");
  auto docA = workspace::TextDocument::create(uriA, "", 1, "alpha\nbeta\n");
  auto docC = std::make_shared<workspace::TextDocument>(
      workspace::TextDocument::create(uriC, "", 1, "unrelated\n"));
  // Only knows about uriC, not the uriB the entry names.
  SingleDocumentProvider provider(docC);

  Diagnostic diagnostic{
      .severity = pegium::DiagnosticSeverity::Error,
      .message = "cross-file",
      .relatedInformation = {{
          .uri = uriB,
          .message = "see other file",
          .begin = 6,
          .end = 10,
      }},
      .begin = 6,
      .end = 10,
  };

  const auto lsp = to_lsp_diagnostic(docA, diagnostic, &provider);
  ASSERT_TRUE(lsp.relatedInformation.has_value());
  ASSERT_EQ(lsp.relatedInformation->size(), 1u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.start.line, 0u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.start.character, 0u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.end.line, 0u);
  EXPECT_EQ(lsp.relatedInformation->front().location.range.end.character, 0u);
}

} // namespace
} // namespace pegium
