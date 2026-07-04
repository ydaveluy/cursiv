#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>
#include <variant>

#include <pegium/lsp/LspExpectTestSupport.hpp>
#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/lsp/completion/DefaultCompletionProvider.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/lsp/services/Services.hpp>

namespace pegium {
namespace {

using namespace pegium::parser;

struct CompletionEntry : pegium::NamedAstNode {};

struct CompletionUse : AstNode {
  reference<CompletionEntry> target;
};

struct CompletionModel : AstNode {
  vector<pointer<CompletionEntry>> entries;
  vector<pointer<CompletionUse>> uses;
};

class CompletionParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return ModelRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<CompletionEntry> EntryRule{"Entry",
                                  "entry"_kw + assign<&CompletionEntry::name>(ID)};
  Rule<CompletionUse> UseRule{"Use",
                              "use"_kw + assign<&CompletionUse::target>(ID)};
  Rule<CompletionModel> ModelRule{
      "Model",
      some(append<&CompletionModel::entries>(EntryRule) |
           append<&CompletionModel::uses>(UseRule))};
#pragma clang diagnostic pop
};

struct FqnPackage : pegium::NamedAstNode {};

struct FqnUse : AstNode {
  reference<FqnPackage> target;
};

struct FqnModel : AstNode {
  vector<pointer<FqnPackage>> packages;
  vector<pointer<FqnUse>> uses;
};

class FqnCompletionParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return ModelRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<std::string> QualifiedName{"QualifiedName", some(ID, "."_kw)};
  Rule<FqnPackage> PackageRule{"Package",
                               "package"_kw + assign<&FqnPackage::name>(QualifiedName)};
  Rule<FqnUse> UseRule{"Use", "use"_kw + assign<&FqnUse::target>(QualifiedName)};
  Rule<FqnModel> ModelRule{
      "Model", some(append<&FqnModel::packages>(PackageRule) |
                    append<&FqnModel::uses>(UseRule))};
#pragma clang diagnostic pop
};

struct KeywordChoiceModel : AstNode {};

class KeywordChoiceParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return ModelRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();
  Rule<KeywordChoiceModel> ModelRule{
      "Model", create<KeywordChoiceModel>() + ("entity"_kw | "enum"_kw)};
#pragma clang diagnostic pop
};

struct UnorderedItem : AstNode {
  string name;
  string type;
};

class UnorderedCompletionParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return ItemRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = skip();
  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<UnorderedItem> ItemRule{
      "Item",
      create<UnorderedItem>() +
          (("name"_kw + assign<&UnorderedItem::name>(ID)) &
           ("type"_kw + assign<&UnorderedItem::type>(ID)))
              .skip(ignored(WS))};
#pragma clang diagnostic pop
};

struct ExpressionNode : AstNode {};
struct DefinitionCompletionNode : AstNode {
  string name;
  string value;
};

struct NameExpression : ExpressionNode {
  string name;
};

struct BinaryExpression : ExpressionNode {
  pointer<ExpressionNode> left;
  string op;
  pointer<ExpressionNode> right;
};

class InfixCompletionParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return RootRule;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = SkipperBuilder().ignore(WS).build();
  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<ExpressionNode> Primary{
      "Primary", create<NameExpression>() + assign<&NameExpression::name>(ID)};
  InfixRule<BinaryExpression, &BinaryExpression::left, &BinaryExpression::op,
            &BinaryExpression::right>
      Expression{"Expression", Primary, LeftAssociation("and"_kw),
                 LeftAssociation("or"_kw)};
  Rule<ExpressionNode> RootRule{"Root", Expression};
#pragma clang diagnostic pop
};

class PunctuationCompletionParser final : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return Definition;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Skipper skipper = skip(ignored(WS));
  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Rule<DefinitionCompletionNode> Definition{
      "Definition",
      "def"_kw + assign<&DefinitionCompletionNode::name>(ID) + ":"_kw +
          assign<&DefinitionCompletionNode::value>(ID)};
#pragma clang diagnostic pop
};

struct TestLiteral final : grammar::Literal {
  explicit TestLiteral(std::string_view value) : value(value) {}

  constexpr bool isNullable() const noexcept override {
    return value.empty();
  }
  std::string_view getValue() const noexcept override { return value; }
  bool isCaseSensitive() const noexcept override { return true; }

  std::string_view value;
};

struct TestNonLiteral final : grammar::AbstractElement {
  constexpr TestNonLiteral() noexcept : AbstractElement(ElementKind::Group) {}

  constexpr bool isNullable() const noexcept override { return false; }

  void print(std::ostream &os) const override { os << "<group>"; }
};

class FilteringCompletionProvider final : public DefaultCompletionProvider {
public:
  using DefaultCompletionProvider::DefaultCompletionProvider;

protected:
  std::vector<const workspace::AstNodeDescription *> getReferenceCandidates(
      const CompletionContext &context, const ReferenceInfo &reference) const override {
    auto candidates = DefaultCompletionProvider::getReferenceCandidates(
        context, reference);
    std::erase_if(candidates, [](const auto *candidate) {
      return !std::string_view(candidate->name).starts_with("A");
    });
    return candidates;
  }
};

class KeywordHookCompletionProvider final : public DefaultCompletionProvider {
public:
  using DefaultCompletionProvider::DefaultCompletionProvider;

protected:
  void completionForKeyword(const CompletionContext &context,
                            const grammar::Literal &keyword,
                            const CompletionAcceptor &acceptor) const override {
    if (keyword.getValue() == "entity") {
      CompletionValue value;
      value.label = "entity";
      value.detail = "Hooked keyword";
      value.kind = ::lsp::CompletionItemKind::Keyword;
      value.sortText = "0";
      acceptor(std::move(value));
      return;
    }
    DefaultCompletionProvider::completionForKeyword(context, keyword, acceptor);
  }
};

class RuleSnippetCompletionProvider final : public DefaultCompletionProvider {
public:
  using DefaultCompletionProvider::DefaultCompletionProvider;

protected:
  void completionForRule(const CompletionContext &context,
                         const grammar::AbstractRule &rule,
                         const CompletionAcceptor &acceptor) const override {
    (void)context;
    if (rule.getName() != "Use") {
      return;
    }

    CompletionValue value;
    value.label = "use snippet";
    value.newText = "use ${1:Target}";
    value.kind = ::lsp::CompletionItemKind::Snippet;
    value.insertTextFormat = ::lsp::InsertTextFormat::Snippet;
    value.sortText = "0";
    acceptor(std::move(value));
  }
};

const ::lsp::CompletionItem *
find_item(const ::lsp::CompletionList &completion, std::string_view label) {
  const auto it =
      std::ranges::find(completion.items, label, &::lsp::CompletionItem::label);
  return it == completion.items.end() ? nullptr : std::addressof(*it);
}

const ::lsp::TextEdit *text_edit(const ::lsp::CompletionItem &item) {
  if (!item.textEdit.has_value()) {
    return nullptr;
  }
  return std::get_if<::lsp::TextEdit>(&*item.textEdit);
}

template <typename Provider, typename ParserType>
std::unique_ptr<pegium::Services> make_services_with_provider(
    const pegium::SharedServices &sharedServices, std::string languageId,
    std::vector<std::string> fileExtensions = {".completion"}) {
  auto services = test::make_uninstalled_services<ParserType>(sharedServices,
                                                  std::move(languageId),
                                                  std::move(fileExtensions));
  pegium::installDefaultCoreServices(*services);
  pegium::installDefaultLspServices(*services);
  services->lsp.completionProvider = std::make_unique<Provider>(*services);
  return services;
}

class DefaultCompletionProviderTest : public ::testing::Test {
protected:
  std::unique_ptr<pegium::SharedServices> shared = test::make_empty_shared_services();

  DefaultCompletionProviderTest() {
    pegium::installDefaultSharedCoreServices(*shared);
    pegium::installDefaultSharedLspServices(*shared);
    pegium::test::initialize_shared_workspace_for_tests(*shared);
  }

  template <typename ParserType>
  void registerParserServices(std::string languageId,
                              std::vector<std::string> fileExtensions) {
    {
      auto registeredServices = 
        test::make_uninstalled_services<ParserType>(*shared, std::move(languageId),
                                        std::move(fileExtensions));
      pegium::installDefaultCoreServices(*registeredServices);
      pegium::installDefaultLspServices(*registeredServices);
      shared->serviceRegistry->registerServices(std::move(registeredServices));
    }
  }

  template <typename Provider, typename ParserType>
  void registerProviderServices(std::string languageId,
                                std::vector<std::string> fileExtensions = {
                                    ".completion"}) {
    auto services = make_services_with_provider<Provider, ParserType>(
        *shared, std::move(languageId), std::move(fileExtensions));
    shared->serviceRegistry->registerServices(std::move(services));
  }

  template <typename Check>
  std::shared_ptr<workspace::Document>
  expectCompletionFor(std::string_view languageId, std::string text,
                      Check &&check) {
    auto document = pegium::test::expectCompletion(
        *shared, languageId,
        pegium::test::ExpectedCompletion{
            .text = std::move(text),
            .check = std::forward<Check>(check),
        });
    EXPECT_NE(document, nullptr);
    return document;
  }
};

TEST_F(DefaultCompletionProviderTest, CompletesKeywordsInEmptyDocument) {
  registerParserServices<KeywordChoiceParser>("keyword", {".keyword"});

  auto document = expectCompletionFor(
      "keyword", "<|>", [](const ::lsp::CompletionList &completion) {
        EXPECT_NE(find_item(completion, "entity"), nullptr);
        EXPECT_NE(find_item(completion, "enum"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest,
       CompletesCommonPrefixKeywordsInsideCurrentToken) {
  registerParserServices<KeywordChoiceParser>("keyword", {".keyword"});

  auto document = expectCompletionFor(
      "keyword", "en<|>", [](const ::lsp::CompletionList &completion) {
        const auto *entity = find_item(completion, "entity");
        const auto *keywordEnum = find_item(completion, "enum");
        ASSERT_NE(entity, nullptr);
        ASSERT_NE(keywordEnum, nullptr);
        ASSERT_TRUE(entity->kind.has_value());
        EXPECT_EQ(*entity->kind, ::lsp::CompletionItemKind::Keyword);
        const auto *edit = text_edit(*entity);
        ASSERT_NE(edit, nullptr);
        EXPECT_EQ(edit->newText, "entity");
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest,
       CompletesNextStatementKeywordsAfterWhitespace) {
  registerParserServices<CompletionParser>("completion", {".completion"});

  auto document = expectCompletionFor(
      "completion",
      "entry Alpha\n"
      "<|>",
      [](const ::lsp::CompletionList &completion) {
        EXPECT_NE(find_item(completion, "entry"), nullptr);
        EXPECT_NE(find_item(completion, "use"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest, CompletesReferenceCandidatesFromScope) {
  registerParserServices<CompletionParser>("completion", {".completion"});

  auto document = expectCompletionFor(
      "completion",
      "entry Alpha\n"
      "entry Beta\n"
      "use Al<|>",
      [](const ::lsp::CompletionList &completion) {
        const auto *alpha = find_item(completion, "Alpha");
        ASSERT_NE(alpha, nullptr);
        ASSERT_TRUE(alpha->detail.has_value());
        EXPECT_NE(std::string_view(*alpha->detail).find("CompletionEntry"),
                  std::string_view::npos);
        ASSERT_TRUE(alpha->kind.has_value());
        EXPECT_EQ(*alpha->kind, ::lsp::CompletionItemKind::Reference);
        EXPECT_EQ(alpha->sortText.value_or(""), "0");
        EXPECT_EQ(find_item(completion, "Beta"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest,
       CompletesFullQualifiedNameAcrossDatatypeRule) {
  registerParserServices<FqnCompletionParser>("fqn", {".fqn"});

  // The cursor sits inside the `foo.ba` qualified name; completion must replace
  // the whole name (from `foo`) with the candidate, not only the `ba` segment.
  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("fqn.fqn"), "fqn",
      "package foo.bar\n"
      "use foo.ba");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.completionProvider, nullptr);

  const auto &text = document->textDocument().getText();
  const auto cursor = static_cast<TextOffset>(text.size());
  ::lsp::CompletionParams params{};
  params.position = document->textDocument().positionAt(cursor);

  const auto completion = services->lsp.completionProvider->getCompletion(
      *document, params, utils::default_cancel_token);
  ASSERT_TRUE(completion.has_value());
  const auto *item = find_item(*completion, "foo.bar");
  ASSERT_NE(item, nullptr);
  const auto *edit = text_edit(*item);
  ASSERT_NE(edit, nullptr);
  EXPECT_EQ(edit->newText, "foo.bar");

  // The replaced range must start at the datatype-rule start (`foo` in `use`).
  const auto useFooOffset =
      static_cast<TextOffset>(text.find("foo", text.find("use")));
  const auto expectedStart = document->textDocument().positionAt(useFooOffset);
  EXPECT_EQ(edit->range.start.line, expectedStart.line);
  EXPECT_EQ(edit->range.start.character, expectedStart.character);
  EXPECT_EQ(edit->range.end.line, params.position.line);
  EXPECT_EQ(edit->range.end.character, params.position.character);
}

TEST_F(DefaultCompletionProviderTest,
       CompletesReferencesWithoutConcreteReferenceInstance) {
  registerParserServices<CompletionParser>("completion", {".completion"});

  auto document = expectCompletionFor(
      "completion",
      "entry Alpha\n"
      "use <|>",
      [](const ::lsp::CompletionList &completion) {
        const auto *alpha = find_item(completion, "Alpha");
        ASSERT_NE(alpha, nullptr);
        ASSERT_TRUE(alpha->kind.has_value());
        EXPECT_EQ(*alpha->kind, ::lsp::CompletionItemKind::Reference);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest, FiltersReferenceCandidatesViaProviderHook) {
  registerProviderServices<FilteringCompletionProvider, CompletionParser>(
      "filtered");

  auto document = expectCompletionFor(
      "filtered",
      "entry Alpha\n"
      "entry Beta\n"
      "use <|>",
      [](const ::lsp::CompletionList &completion) {
        EXPECT_NE(find_item(completion, "Alpha"), nullptr);
        EXPECT_EQ(find_item(completion, "Beta"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest, SupportsKeywordCompletionHook) {
  registerProviderServices<KeywordHookCompletionProvider, KeywordChoiceParser>(
      "hooked-keyword", {".keyword"});

  auto document = expectCompletionFor(
      "hooked-keyword", "<|>", [](const ::lsp::CompletionList &completion) {
        const auto *entity = find_item(completion, "entity");
        ASSERT_NE(entity, nullptr);
        EXPECT_EQ(entity->detail.value_or(""), "Hooked keyword");
        EXPECT_NE(find_item(completion, "enum"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest, SupportsRuleSnippetHook) {
  registerProviderServices<RuleSnippetCompletionProvider, CompletionParser>(
      "snippet");

  auto document = expectCompletionFor(
      "snippet",
      "entry Alpha\n"
      "<|>",
      [](const ::lsp::CompletionList &completion) {
        const auto *snippet = find_item(completion, "use snippet");
        ASSERT_NE(snippet, nullptr);
        ASSERT_TRUE(snippet->kind.has_value());
        EXPECT_EQ(*snippet->kind, ::lsp::CompletionItemKind::Snippet);
        ASSERT_TRUE(snippet->insertTextFormat.has_value());
        EXPECT_EQ(*snippet->insertTextFormat,
                  ::lsp::InsertTextFormat::Snippet);
        const auto *edit = text_edit(*snippet);
        ASSERT_NE(edit, nullptr);
        EXPECT_EQ(edit->newText, "use ${1:Target}");
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest,
       CompletesRemainingKeywordInUnorderedGroupWithLocalSkipper) {
  registerParserServices<UnorderedCompletionParser>("unordered", {".unordered"});

  auto document = expectCompletionFor(
      "unordered", "name Foo   <|>", [](const ::lsp::CompletionList &completion) {
        const auto *type = find_item(completion, "type");
        ASSERT_NE(type, nullptr);
        ASSERT_TRUE(type->kind.has_value());
        EXPECT_EQ(*type->kind, ::lsp::CompletionItemKind::Keyword);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest, CompletesInfixOperators) {
  registerParserServices<InfixCompletionParser>("infix", {".infix"});

  auto document = expectCompletionFor(
      "infix", "foo <|>", [](const ::lsp::CompletionList &completion) {
        EXPECT_NE(find_item(completion, "and"), nullptr);
        EXPECT_NE(find_item(completion, "or"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest, CompletesPunctuationKeywordWhenExpected) {
  registerParserServices<PunctuationCompletionParser>("punctuation",
                                                      {".punctuation"});

  auto document = expectCompletionFor(
      "punctuation", "def value <|>",
      [](const ::lsp::CompletionList &completion) {
        EXPECT_NE(find_item(completion, ":"), nullptr);
        EXPECT_EQ(find_item(completion, ";"), nullptr);
        EXPECT_EQ(find_item(completion, "def"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest,
       CompletesRuleSnippetWhenIdentifierIsExpected) {
  registerParserServices<PunctuationCompletionParser>("punctuation",
                                                      {".punctuation"});

  auto document = expectCompletionFor(
      "punctuation", "def <|>", [](const ::lsp::CompletionList &completion) {
        const auto *identifier = find_item(completion, "ID");
        ASSERT_NE(identifier, nullptr);
        ASSERT_TRUE(identifier->kind.has_value());
        EXPECT_EQ(*identifier->kind, ::lsp::CompletionItemKind::Snippet);
        ASSERT_TRUE(identifier->insertTextFormat.has_value());
        EXPECT_EQ(*identifier->insertTextFormat,
                  ::lsp::InsertTextFormat::Snippet);
        const auto *edit = text_edit(*identifier);
        ASSERT_NE(edit, nullptr);
        EXPECT_EQ(edit->newText, "${1:ID}");
        EXPECT_EQ(find_item(completion, ":"), nullptr);
      });
  ASSERT_NE(document, nullptr);
}

TEST_F(DefaultCompletionProviderTest,
       CompletesRecoveredDocumentWithoutDiagnosticFallback) {
  registerParserServices<PunctuationCompletionParser>("punctuation",
                                                      {".punctuation"});

  auto document = expectCompletionFor(
      "punctuation", "def value <|>", [](const ::lsp::CompletionList &completion) {
        const auto *colon = find_item(completion, ":");
        ASSERT_NE(colon, nullptr);
      });
  ASSERT_NE(document, nullptr);
  EXPECT_FALSE(document->parseResult.parseDiagnostics.empty());
}

} // namespace
} // namespace pegium
