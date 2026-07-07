#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/lsp/semantic/AbstractSemanticTokenProvider.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/lsp/services/Services.hpp>

namespace pegium {
namespace {
using namespace pegium::parser;

struct SemanticEntry : AstNode {
  string name;
};

struct QuotedSemanticEntry : AstNode {
  string value;
};

class SemanticParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return EntryRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<SemanticEntry> EntryRule{"Entry",
                                "entry"_kw + assign<&SemanticEntry::name>(ID)};
#pragma clang diagnostic pop
};

class QuotedSemanticParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return EntryRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> TEXT{"TEXT", "\""_kw <=> "\""_kw};
  Rule<QuotedSemanticEntry> EntryRule{
      "Entry", "entry"_kw + assign<&QuotedSemanticEntry::value>(TEXT)};
#pragma clang diagnostic pop
};

class TestSemanticTokenProvider final : public AbstractSemanticTokenProvider {
public:
  explicit TestSemanticTokenProvider(const pegium::Services &services)
      : AbstractSemanticTokenProvider(services) {}

protected:
  void highlightElement(const AstNode &node,
                        const SemanticTokenAcceptor &acceptor) const override {
    if (dynamic_cast<const SemanticEntry *>(&node) == nullptr) {
      return;
    }
    highlightKeyword(node, "entry", "keyword", acceptor);
    highlightProperty(node, "name", "variable", acceptor, {"declaration"});
  }
};

struct RepeatedKeywordEntry : AstNode {};

class RepeatedKeywordParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return EntryRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Rule<RepeatedKeywordEntry> EntryRule{"Entry",
                                      "mark"_kw + "mark"_kw + "mark"_kw};
#pragma clang diagnostic pop
};

class RepeatedKeywordTokenProvider final : public AbstractSemanticTokenProvider {
public:
  explicit RepeatedKeywordTokenProvider(const pegium::Services &services)
      : AbstractSemanticTokenProvider(services) {}

protected:
  void highlightElement(const AstNode &node,
                        const SemanticTokenAcceptor &acceptor) const override {
    if (dynamic_cast<const RepeatedKeywordEntry *>(&node) == nullptr) {
      return;
    }
    // Enumerate-all path (no explicit index): must highlight every occurrence
    // exactly once and terminate.
    highlightKeyword(node, "mark", "keyword", acceptor);
  }
};

class IndexedKeywordTokenProvider final : public AbstractSemanticTokenProvider {
public:
  IndexedKeywordTokenProvider(const pegium::Services &services, std::size_t index)
      : AbstractSemanticTokenProvider(services), _index(index) {}

protected:
  void highlightElement(const AstNode &node,
                        const SemanticTokenAcceptor &acceptor) const override {
    if (dynamic_cast<const RepeatedKeywordEntry *>(&node) == nullptr) {
      return;
    }
    highlightKeyword(node, "mark", "keyword", acceptor, {}, _index);
  }

private:
  std::size_t _index;
};

class TestQuotedSemanticTokenProvider final : public AbstractSemanticTokenProvider {
public:
  explicit TestQuotedSemanticTokenProvider(const pegium::Services &services)
      : AbstractSemanticTokenProvider(services) {}

protected:
  void highlightElement(const AstNode &node,
                        const SemanticTokenAcceptor &acceptor) const override {
    if (dynamic_cast<const QuotedSemanticEntry *>(&node) == nullptr) {
      return;
    }
    highlightProperty(node, "value", "string", acceptor);
  }
};

struct DecodedSemanticToken {
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  std::uint32_t length = 0;
  std::uint32_t type = 0;
  std::uint32_t modifiers = 0;
};

std::vector<DecodedSemanticToken>
decode_semantic_tokens(const ::lsp::Array<std::uint32_t> &data) {
  std::vector<DecodedSemanticToken> tokens;
  std::uint32_t line = 0;
  std::uint32_t character = 0;

  for (std::size_t index = 0; index + 4 < data.size(); index += 5) {
    line += data[index];
    character = data[index] == 0 ? character + data[index + 1] : data[index + 1];
    tokens.push_back({.line = line,
                      .character = character,
                      .length = data[index + 2],
                      .type = data[index + 3],
                      .modifiers = data[index + 4]});
  }

  return tokens;
}

void initialize_multiline_semantic_tokens(pegium::SharedServices &shared,
                                          bool enabled) {
  ASSERT_NE(shared.lsp.languageServer, nullptr);

  ::lsp::InitializeParams params{};
  params.capabilities.textDocument.emplace();
  params.capabilities.textDocument->semanticTokens.emplace();
  params.capabilities.textDocument->semanticTokens->multilineTokenSupport =
      enabled;

  (void)shared.lsp.languageServer->initialize(params);
}

TEST(AbstractSemanticTokenProviderTest, ExposesLegendAndEncodesTokens) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<SemanticParser>(*shared, "semantic", {".semantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic.semantic"), "semantic",
      "entry Foo");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestSemanticTokenProvider provider(*services);

  const auto options = provider.semanticTokensOptions();
  EXPECT_FALSE(options.legend.tokenTypes.empty());
  EXPECT_FALSE(options.legend.tokenModifiers.empty());
  ASSERT_TRUE(options.full.has_value());
  ASSERT_TRUE(std::holds_alternative<::lsp::SemanticTokensOptionsFull>(*options.full));
  EXPECT_TRUE(options.range.has_value());

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());
  EXPECT_TRUE(tokens->resultId.has_value());
  EXPECT_EQ(tokens->data.size(), 10u);
}

TEST(AbstractSemanticTokenProviderTest,
     HighlightKeywordEnumeratesEveryRepeatedOccurrenceWithoutHanging) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<RepeatedKeywordParser>(
        *shared, "repeated-keyword", {".repeated"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("repeated.repeated"), "repeated-keyword",
      "mark mark mark");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  RepeatedKeywordTokenProvider provider(*services);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());
  // 3 occurrences of "mark", 5 uint32 values per token -> 15 total.
  EXPECT_EQ(tokens->data.size(), 15u);
}

TEST(AbstractSemanticTokenProviderTest,
     HighlightKeywordValidIndexHighlightsOnlyThatOccurrence) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<RepeatedKeywordParser>(
        *shared, "repeated-keyword", {".repeated"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("repeated.repeated"), "repeated-keyword",
      "mark mark mark");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  IndexedKeywordTokenProvider provider(*services, 1);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());
  // Exactly the second "mark" is highlighted: one token.
  EXPECT_EQ(tokens->data.size(), 5u);
}

TEST(AbstractSemanticTokenProviderTest,
     HighlightKeywordOutOfRangeIndexHighlightsNothing) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<RepeatedKeywordParser>(
        *shared, "repeated-keyword", {".repeated"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("repeated.repeated"), "repeated-keyword",
      "mark mark mark");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  // Only 3 occurrences exist; index 5 must not fall back to the last one.
  IndexedKeywordTokenProvider provider(*services, 5);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());
  EXPECT_EQ(tokens->data.size(), 0u);
}

TEST(AbstractSemanticTokenProviderTest, EncodesAsciiTokenLengthInUtf16Units) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<QuotedSemanticParser>(
        *shared, "quoted-semantic", {".qsemantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic-ascii.qsemantic"),
      "quoted-semantic", "entry \"Foo\"");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestQuotedSemanticTokenProvider provider(*services);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());

  const auto decoded = decode_semantic_tokens(tokens->data);
  const auto tokenTypes = provider.tokenTypes();
  const auto it = std::ranges::find_if(decoded, [&tokenTypes](const auto &token) {
    return token.type == tokenTypes.at("string");
  });
  ASSERT_NE(it, decoded.end());
  EXPECT_EQ(it->line, 0u);
  EXPECT_EQ(it->character, 6u);
  EXPECT_EQ(it->length, 5u);
}

TEST(AbstractSemanticTokenProviderTest, EncodesAccentTokenLengthInUtf16Units) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<QuotedSemanticParser>(
        *shared, "quoted-semantic", {".qsemantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic-accent.qsemantic"),
      "quoted-semantic", "entry \"Café\"");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestQuotedSemanticTokenProvider provider(*services);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());

  const auto decoded = decode_semantic_tokens(tokens->data);
  const auto tokenTypes = provider.tokenTypes();
  const auto it = std::ranges::find_if(decoded, [&tokenTypes](const auto &token) {
    return token.type == tokenTypes.at("string");
  });
  ASSERT_NE(it, decoded.end());
  EXPECT_EQ(it->length, 6u);
}

TEST(AbstractSemanticTokenProviderTest, EncodesEmojiTokenLengthInUtf16Units) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<QuotedSemanticParser>(
        *shared, "quoted-semantic", {".qsemantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic-emoji.qsemantic"),
      "quoted-semantic", "entry \"😀\"");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestQuotedSemanticTokenProvider provider(*services);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());

  const auto decoded = decode_semantic_tokens(tokens->data);
  const auto tokenTypes = provider.tokenTypes();
  const auto it = std::ranges::find_if(decoded, [&tokenTypes](const auto &token) {
    return token.type == tokenTypes.at("string");
  });
  ASSERT_NE(it, decoded.end());
  EXPECT_EQ(it->length, 4u);
}

TEST(AbstractSemanticTokenProviderTest,
     KeepsMultilineTokenWholeWhenClientSupportsIt) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<QuotedSemanticParser>(
        *shared, "quoted-semantic", {".qsemantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic-multiline.qsemantic"),
      "quoted-semantic", "entry \"ab\ncd\"");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestQuotedSemanticTokenProvider provider(*services);
  initialize_multiline_semantic_tokens(*shared, true);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());

  const auto decoded = decode_semantic_tokens(tokens->data);
  ASSERT_EQ(decoded.size(), 1u);
  EXPECT_EQ(decoded[0].line, 0u);
  EXPECT_EQ(decoded[0].character, 6u);
  EXPECT_EQ(decoded[0].length, 7u);
}

TEST(AbstractSemanticTokenProviderTest,
     SplitsMultilineTokenWhenClientDoesNotSupportIt) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = test::make_uninstalled_services<QuotedSemanticParser>(
        *shared, "quoted-semantic", {".qsemantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic-multiline-split.qsemantic"),
      "quoted-semantic", "entry \"ab\ncd\"");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestQuotedSemanticTokenProvider provider(*services);
  initialize_multiline_semantic_tokens(*shared, false);

  ::lsp::SemanticTokensParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));

  const auto tokens =
      provider.semanticHighlight(*document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());

  const auto decoded = decode_semantic_tokens(tokens->data);
  ASSERT_EQ(decoded.size(), 2u);
  EXPECT_EQ(decoded[0].line, 0u);
  EXPECT_EQ(decoded[0].character, 6u);
  EXPECT_EQ(decoded[0].length, 3u);
  EXPECT_EQ(decoded[1].line, 1u);
  EXPECT_EQ(decoded[1].character, 0u);
  EXPECT_EQ(decoded[1].length, 3u);
}

TEST(AbstractSemanticTokenProviderTest, RangeHighlightFiltersTokens) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<SemanticParser>(*shared, "semantic", {".semantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic-range.semantic"), "semantic",
      "entry Foo");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestSemanticTokenProvider provider(*services);

  ::lsp::SemanticTokensRangeParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.range.start.line = 0;
  params.range.start.character = 6;
  params.range.end.line = 0;
  params.range.end.character = 9;

  const auto tokens = provider.semanticHighlightRange(
      *document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());
  EXPECT_TRUE(tokens->resultId.has_value());
  EXPECT_EQ(tokens->data.size(), 5u);
}

TEST(AbstractSemanticTokenProviderTest, DeltaFallsBackToFullForUnknownSnapshot) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<SemanticParser>(*shared, "semantic", {".semantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("semantic-delta-full.semantic"), "semantic",
      "entry Foo");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestSemanticTokenProvider provider(*services);

  ::lsp::SemanticTokensDeltaParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.previousResultId = "missing";

  const auto tokens = provider.semanticHighlightDelta(
      *document, params, utils::default_cancel_token);
  ASSERT_TRUE(tokens.has_value());
  ASSERT_TRUE(std::holds_alternative<::lsp::SemanticTokens>(*tokens));
  const auto &full = std::get<::lsp::SemanticTokens>(*tokens);
  EXPECT_TRUE(full.resultId.has_value());
  EXPECT_EQ(full.data.size(), 10u);
}

TEST(AbstractSemanticTokenProviderTest,
     DeltaReturnsEditsForKnownSnapshotAndClearsCacheOnClose) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services<SemanticParser>(*shared, "semantic", {".semantic"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  const auto uri = test::make_file_uri("semantic-delta.semantic");
  auto document =
      test::open_and_build_document(*shared, uri, "semantic", "entry Foo");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);

  TestSemanticTokenProvider provider(*services);

  ::lsp::SemanticTokensParams fullParams{};
  fullParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  const auto full =
      provider.semanticHighlight(*document, fullParams, utils::default_cancel_token);
  ASSERT_TRUE(full.has_value());
  ASSERT_TRUE(full->resultId.has_value());

  ::lsp::SemanticTokensDeltaParams deltaParams{};
  deltaParams.textDocument.uri =
      ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  deltaParams.previousResultId = *full->resultId;

  const auto delta = provider.semanticHighlightDelta(
      *document, deltaParams, utils::default_cancel_token);
  ASSERT_TRUE(delta.has_value());
  ASSERT_TRUE(std::holds_alternative<::lsp::SemanticTokensDelta>(*delta));
  const auto &edits = std::get<::lsp::SemanticTokensDelta>(*delta);
  EXPECT_TRUE(edits.resultId.has_value());
  EXPECT_TRUE(edits.edits.empty());

  auto documents = test::text_documents(*shared);
  ASSERT_TRUE(documents != nullptr);
  documents->remove(document->uri);

  const auto afterClose = provider.semanticHighlightDelta(
      *document, deltaParams, utils::default_cancel_token);
  ASSERT_TRUE(afterClose.has_value());
  EXPECT_TRUE(std::holds_alternative<::lsp::SemanticTokens>(*afterClose));
}

} // namespace
} // namespace pegium
