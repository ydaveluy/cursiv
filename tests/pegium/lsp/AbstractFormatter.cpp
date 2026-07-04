#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/lsp/formatting/AbstractFormatter.hpp>
#include <pegium/lsp/services/ServiceAccess.hpp>
#include <pegium/core/parser/PegiumParser.hpp>

namespace pegium {
namespace {

using namespace pegium::parser;

namespace mini::ast {

struct Field : pegium::AstNode {
  bool many = false;
  string name;
  string type;
};

struct Item : pegium::AstNode {
  string name;
  vector<pointer<Field>> fields;
};

struct Model : pegium::AstNode {
  vector<pointer<Item>> items;
};

} // namespace mini::ast

class MiniParser final : public parser::PegiumParser {
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
  Terminal<> SL_COMMENT{"SL_COMMENT", "//"_kw <=> &(eol | eof)};
  Terminal<> ML_COMMENT{"ML_COMMENT", "/*"_kw <=> "*/"_kw};
  Skipper skipper =
      SkipperBuilder().ignore(WS).hide(ML_COMMENT, SL_COMMENT).build();

  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};

  Rule<mini::ast::Field> FieldRule{
      "Field",
      option(enable_if<&mini::ast::Field::many>("many"_kw.i())) +
          assign<&mini::ast::Field::name>(ID) + ":"_kw +
          assign<&mini::ast::Field::type>(ID)};

  Rule<mini::ast::Item> ItemRule{
      "Item",
      "entity"_kw.i() + assign<&mini::ast::Item::name>(ID) + "{"_kw +
          many(append<&mini::ast::Item::fields>(FieldRule)) + "}"_kw};

  Rule<mini::ast::Model> ModelRule{
      "Model", some(append<&mini::ast::Model::items>(ItemRule))};
#pragma clang diagnostic pop
};

const pegium::Services &lookup_services(const pegium::SharedServices &shared,
                                        std::string_view languageId) {
  for (const auto *coreServices : shared.serviceRegistry->all()) {
    if (coreServices == nullptr ||
        coreServices->languageMetaData.languageId != languageId) {
      continue;
    }
    const auto *services = as_services(coreServices);
    if (services == nullptr) {
      throw std::runtime_error("Invalid formatter test services type.");
    }
    return *services;
  }
  throw std::runtime_error("Missing language services for formatter test.");
}

const mini::ast::Model *model_of(const workspace::Document &document) {
  return pegium::ast_ptr_cast<mini::ast::Model>(document.parseResult.value);
}

struct ParsedMiniDocument {
  std::unique_ptr<pegium::SharedServices> shared;
  std::shared_ptr<workspace::Document> document;
};

ParsedMiniDocument open_mini_document(std::string text) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services = test::make_uninstalled_services<MiniParser>(*shared, "mini", {".mini"});
  pegium::installDefaultCoreServices(*services);
  pegium::installDefaultLspServices(*services);
  shared->serviceRegistry->registerServices(std::move(services));

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("mini-format.test"), "mini", std::move(text));
  if (document == nullptr) {
    throw std::runtime_error("Failed to parse mini formatter document.");
  }
  return ParsedMiniDocument{
      .shared = std::move(shared),
      .document = std::move(document),
  };
}

std::string apply_text_edits(const workspace::Document &document,
                             std::vector<::lsp::TextEdit> edits) {
  const auto &textDocument = document.textDocument();
  auto text = std::string(textDocument.getText());
  std::ranges::sort(edits, [&document](const auto &left, const auto &right) {
    return document.textDocument().offsetAt(left.range.start) >
           document.textDocument().offsetAt(right.range.start);
  });

  for (const auto &edit : edits) {
    const auto begin = textDocument.offsetAt(edit.range.start);
    const auto end = textDocument.offsetAt(edit.range.end);
    text.replace(begin, end - begin, edit.newText);
  }
  return text;
}

class CountingFormatter final : public AbstractFormatter {
public:
  using AbstractFormatter::AbstractFormatter;

  mutable std::size_t callCount = 0;

protected:
  void format(FormattingBuilder &, const AstNode *) const override { ++callCount; }
};

class KeywordSpacingFormatter final : public AbstractFormatter {
public:
  explicit KeywordSpacingFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Item>(&KeywordSpacingFormatter::formatItem);
  }

protected:
  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("entity").append(oneSpace);
  }
};

class PriorityFormatter final : public AbstractFormatter {
public:
  explicit PriorityFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Item>(&PriorityFormatter::formatItem);
  }

protected:
  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("entity").append(spaces(3, {.priority = 0}));
    formatter.keyword("entity").append(oneSpace({.priority = 2}));
  }
};

class OverlapFormatter final : public AbstractFormatter {
public:
  explicit OverlapFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Item>(&OverlapFormatter::formatItem);
  }

protected:
  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("entity").append(spaces(3));
    formatter.property<&mini::ast::Item::name>().prepend(oneSpace);
  }
};

class AdjacentGapFormatter final : public AbstractFormatter {
public:
  explicit AdjacentGapFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Field>(&AdjacentGapFormatter::formatField);
  }

protected:
  virtual void formatField(FormattingBuilder &builder,
                           const mini::ast::Field *field) const {
    auto formatter = builder.getNodeFormatter(field);
    // Both rules target the same zero-width gap between an adjacent `name` and
    // `:` (input "a:b"): the name's trailing edit and the colon's leading edit.
    formatter.property<&mini::ast::Field::name>().append(oneSpace);
    formatter.keyword(":").prepend(oneSpace);
  }
};

class FlexibleWhitespaceFormatter final : public AbstractFormatter {
public:
  explicit FlexibleWhitespaceFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Item>(&FlexibleWhitespaceFormatter::formatItem);
  }

protected:
  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("entity").append(oneSpace({.allowMore = true}));
    formatter.keyword("{").prepend(newLine({.allowLess = true}));
  }
};

class FitFormatter final : public AbstractFormatter {
public:
  explicit FitFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Item>(&FitFormatter::formatItem);
  }

protected:
  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("{").prepend(fit(oneSpace, newLine));
  }
};

class MiniLayoutFormatter final : public AbstractFormatter {
public:
  explicit MiniLayoutFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Model>(&MiniLayoutFormatter::formatModel);
    on<mini::ast::Item>(&MiniLayoutFormatter::formatItem);
    on<mini::ast::Field>(&MiniLayoutFormatter::formatField);
  }

protected:
  virtual void formatModel(FormattingBuilder &builder,
                           const mini::ast::Model *model) const {
    auto formatter = builder.getNodeFormatter(model);
    formatter.properties<&mini::ast::Model::items>().prepend(noIndent);
  }

  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("entity").append(oneSpace);
    const auto openBrace = formatter.keyword("{");
    const auto closeBrace = formatter.keyword("}");
    formatBlock(openBrace, closeBrace, formatter.interior(openBrace, closeBrace));
  }

  virtual void formatField(FormattingBuilder &builder,
                           const mini::ast::Field *field) const {
    auto formatter = builder.getNodeFormatter(field);
    if (field->many) {
      formatter.keyword("many").append(oneSpace);
    }
    formatter.keyword(":").prepend(noSpace).append(oneSpace);
  }
};

class HiddenSelectionFormatter final : public AbstractFormatter {
public:
  explicit HiddenSelectionFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Model>(&HiddenSelectionFormatter::formatModel);
    on<mini::ast::Item>(&HiddenSelectionFormatter::formatItem);
    on<mini::ast::Field>(&HiddenSelectionFormatter::formatField);
  }

protected:
  virtual void formatModel(FormattingBuilder &builder,
                           const mini::ast::Model *model) const {
    auto formatter = builder.getNodeFormatter(model);
    formatter.properties<&mini::ast::Model::items>().prepend(noIndent);
  }

  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("entity").append(oneSpace);
    const auto openBrace = formatter.keyword("{");
    const auto closeBrace = formatter.keyword("}");
    formatBlock(openBrace, closeBrace, formatter.properties<&mini::ast::Item::fields>(),
                {.beforeOpen = oneSpace, .insideEmpty = noSpace,
                 .afterOpen = newLine, .beforeClose = newLine,
                 .contentIndent = indent});
    formatter.hiddens("SL_COMMENT").prepend(indent);
  }

  virtual void formatField(FormattingBuilder &builder,
                           const mini::ast::Field *field) const {
    auto formatter = builder.getNodeFormatter(field);
    if (field->many) {
      formatter.keyword("many").append(oneSpace);
    }
    formatter.keyword(":").prepend(noSpace).append(oneSpace);
  }
};

class HiddenReplacementFormatter final : public AbstractFormatter {
public:
  explicit HiddenReplacementFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Model>(&HiddenReplacementFormatter::formatModel);
    on<mini::ast::Item>(&HiddenReplacementFormatter::formatItem);
    on<mini::ast::Field>(&HiddenReplacementFormatter::formatField);
    onHidden("ML_COMMENT", &HiddenReplacementFormatter::formatComment);
  }

protected:
  virtual void formatModel(FormattingBuilder &builder,
                           const mini::ast::Model *model) const {
    auto formatter = builder.getNodeFormatter(model);
    formatter.properties<&mini::ast::Model::items>().prepend(noIndent);
  }

  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    formatter.keyword("entity").append(oneSpace);
    const auto openBrace = formatter.keyword("{");
    const auto closeBrace = formatter.keyword("}");
    formatBlock(openBrace, closeBrace, formatter.interior(openBrace, closeBrace));
  }

  virtual void formatField(FormattingBuilder &builder,
                           const mini::ast::Field *field) const {
    auto formatter = builder.getNodeFormatter(field);
    if (field->many) {
      formatter.keyword("many").append(oneSpace);
    }
    formatter.keyword(":").prepend(noSpace).append(oneSpace);
  }

  virtual void formatComment(HiddenNodeFormatter &comment) const {
    comment.replace("/* level " + std::to_string(comment.baseIndentation()) +
                    "\n * updated\n */");
  }
};

class ExactDispatchFormatter final : public AbstractFormatter {
public:
  explicit ExactDispatchFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<AstNode>(&ExactDispatchFormatter::formatAstNode);
    on<mini::ast::Model>(&ExactDispatchFormatter::formatModel);
    on<mini::ast::Item>(&ExactDispatchFormatter::formatItem);
    on<mini::ast::Field>(&ExactDispatchFormatter::formatField);
  }

  mutable std::size_t astNodeCount = 0;
  mutable std::size_t modelCount = 0;
  mutable std::size_t itemCount = 0;
  mutable std::size_t fieldCount = 0;

protected:
  virtual void formatAstNode(FormattingBuilder &, const AstNode *) const {
    ++astNodeCount;
  }
  virtual void formatModel(FormattingBuilder &,
                           const mini::ast::Model *) const {
    ++modelCount;
  }
  virtual void formatItem(FormattingBuilder &,
                          const mini::ast::Item *) const {
    ++itemCount;
  }
  virtual void formatField(FormattingBuilder &,
                           const mini::ast::Field *) const {
    ++fieldCount;
  }
};

class OverridingRegistrationFormatter final : public AbstractFormatter {
public:
  explicit OverridingRegistrationFormatter(const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Item>(&OverridingRegistrationFormatter::formatItemWithThreeSpaces);
    on<mini::ast::Item>(&OverridingRegistrationFormatter::formatItemWithOneSpace);
  }

protected:
  virtual void formatItemWithThreeSpaces(FormattingBuilder &builder,
                                         const mini::ast::Item *item) const {
    builder.getNodeFormatter(item).keyword("entity").append(spaces(3));
  }

  virtual void formatItemWithOneSpace(FormattingBuilder &builder,
                                      const mini::ast::Item *item) const {
    builder.getNodeFormatter(item).keyword("entity").append(oneSpace);
  }
};

class OverridingHiddenRegistrationFormatter final : public AbstractFormatter {
public:
  explicit OverridingHiddenRegistrationFormatter(
      const pegium::Services &services)
      : AbstractFormatter(services) {
    on<mini::ast::Model>(&OverridingHiddenRegistrationFormatter::formatModel);
    on<mini::ast::Item>(&OverridingHiddenRegistrationFormatter::formatItem);
    onHidden("ML_COMMENT", &OverridingHiddenRegistrationFormatter::formatCommentFirst);
    onHidden("ML_COMMENT", &OverridingHiddenRegistrationFormatter::formatCommentSecond);
  }

protected:
  virtual void formatModel(FormattingBuilder &builder,
                           const mini::ast::Model *model) const {
    builder.getNodeFormatter(model)
        .properties<&mini::ast::Model::items>()
        .prepend(noIndent);
  }

  virtual void formatItem(FormattingBuilder &builder,
                          const mini::ast::Item *item) const {
    auto formatter = builder.getNodeFormatter(item);
    const auto openBrace = formatter.keyword("{");
    const auto closeBrace = formatter.keyword("}");
    formatBlock(openBrace, closeBrace, formatter.interior(openBrace, closeBrace));
  }

  virtual void formatCommentFirst(HiddenNodeFormatter &comment) const {
    comment.replace("/** first */");
  }

  virtual void formatCommentSecond(HiddenNodeFormatter &comment) const {
    comment.replace(
        formatMultilineComment(comment,
                               {.forceMultiline = true, .maxBlankLines = 0}));
  }
};

class FormattingApiProbe final : public AbstractFormatter {
public:
  using AbstractFormatter::AbstractFormatter;

  [[nodiscard]] static FormattingAction oneSpaceAction(
      FormattingActionOptions options = {}) {
    return oneSpace(std::move(options));
  }

  [[nodiscard]] static std::string
  formatComment(std::string_view text,
                const MultilineCommentFormatOptions &options = {}) {
    return formatMultilineComment(text, options);
  }

  [[nodiscard]] static std::string
  formatLine(std::string_view text, LineCommentFormatOptions options = {}) {
    return formatLineComment(text, std::move(options));
  }
};

TEST(AbstractFormatterTest, FullFormattingSkipsRecoveredDocuments) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services(*shared, "mini", {".mini"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  workspace::Document document(
      test::make_text_document(test::make_file_uri("recovered.mini"), "mini",
                               "entity A {}"));
  document.parseResult.parseDiagnostics.push_back(parser::ParseDiagnostic{.offset = 0});

  CountingFormatter provider(lookup_services(*shared, "mini"));
  const auto edits = provider.formatDocument(
      document, ::lsp::DocumentFormattingParams{}, utils::default_cancel_token);

  EXPECT_TRUE(edits.empty());
  EXPECT_EQ(provider.callCount, 0u);
}

TEST(AbstractFormatterTest, RangeFormattingSkipsRangesWithEarlierParseErrors) {
  auto shared = test::make_empty_shared_services();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_services(*shared, "mini", {".mini"});
    pegium::installDefaultCoreServices(*registeredServices);
    pegium::installDefaultLspServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  workspace::Document document(test::make_text_document(
      test::make_file_uri("range-error.mini"), "mini",
      "entity A {}\nentity B {}"));
  document.parseResult.parseDiagnostics.push_back(parser::ParseDiagnostic{.offset = 0});

  CountingFormatter provider(lookup_services(*shared, "mini"));
  ::lsp::DocumentRangeFormattingParams params{};
  params.range.start = text::Position(1, 0);
  params.range.end = text::Position(1, 10);

  const auto edits =
      provider.formatDocumentRange(document, params, utils::default_cancel_token);
  EXPECT_TRUE(edits.empty());
  EXPECT_EQ(provider.callCount, 0u);
}

TEST(AbstractFormatterTest, RegisteredCallbacksUseExactRuntimeTypeDispatch) {
  auto parsed = open_mini_document(
      "entity Alpha { first:T second:U }\n"
      "entity Beta {}");
  ASSERT_NE(parsed.document, nullptr);

  ExactDispatchFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits = provider.formatDocument(
      *parsed.document, ::lsp::DocumentFormattingParams{},
      utils::default_cancel_token);

  EXPECT_TRUE(edits.empty());
  EXPECT_EQ(provider.astNodeCount, 0u);
  EXPECT_EQ(provider.modelCount, 1u);
  EXPECT_EQ(provider.itemCount, 2u);
  EXPECT_EQ(provider.fieldCount, 2u);
}

TEST(AbstractFormatterTest, LaterFormatterRegistrationOverridesEarlierOne) {
  auto parsed = open_mini_document("entity   Alpha {}");
  ASSERT_NE(parsed.document, nullptr);

  OverridingRegistrationFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits = provider.formatDocument(
      *parsed.document, ::lsp::DocumentFormattingParams{},
      utils::default_cancel_token);

  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(apply_text_edits(*parsed.document, edits), "entity Alpha {}");
}

TEST(AbstractFormatterTest, LaterHiddenFormatterRegistrationOverridesEarlierOne) {
  auto parsed = open_mini_document(
      "entity Alpha {\n"
      "/**old\n"
      "@param   x   value\n"
      "*/\n"
      "}");
  ASSERT_NE(parsed.document, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;

  OverridingHiddenRegistrationFormatter provider(
      lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*parsed.document, edits),
            "entity Alpha {\n"
            "  /**\n"
            "   * old\n"
            "   * @param x value\n"
            "   */\n"
            "}");
}

TEST(AbstractFormatterDslTest,
     TypedPropertyAndKeywordSelectionsTargetExpectedCstNodes) {
  auto parsed = open_mini_document("entity Alpha { many first:T second:U }");
  ASSERT_NE(parsed.document, nullptr);
  const auto *model = model_of(*parsed.document);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->items.size(), 1u);
  const auto &item = *model->items.front();

  std::vector<std::string> selected;
  FormattingBuilder builder([&selected](const CstNodeView &node, FormattingMode,
                                        const FormattingAction &) {
    selected.emplace_back(node.getText());
  });
  auto formatter = builder.getNodeFormatter(&item);
  const auto action = FormattingApiProbe::oneSpaceAction();

  formatter.property<&mini::ast::Item::name>().prepend(action);
  formatter.property<&mini::ast::Item::fields>(1).prepend(action);
  formatter.keyword("entity").prepend(action);
  formatter.keywords("{", "}").prepend(action);

  ASSERT_EQ(selected.size(), 5u);
  EXPECT_EQ(selected[0], "Alpha");
  EXPECT_EQ(selected[1], "second:U");
  EXPECT_EQ(selected[2], "entity");
  EXPECT_EQ(selected[3], "{");
  EXPECT_EQ(selected[4], "}");
}

TEST(AbstractFormatterDslTest,
     InteriorAndSliceSelectionsReturnExpectedNodes) {
  auto parsed = open_mini_document("entity Alpha { first:T second:U third:V }");
  ASSERT_NE(parsed.document, nullptr);
  const auto *model = model_of(*parsed.document);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->items.size(), 1u);
  const auto &item = *model->items.front();

  std::vector<std::string> selected;
  FormattingBuilder builder([&selected](const CstNodeView &node, FormattingMode,
                                        const FormattingAction &) {
    selected.emplace_back(node.getText());
  });

  auto formatter = builder.getNodeFormatter(&item);
  const auto openBrace = formatter.keyword("{");
  const auto closeBrace = formatter.keyword("}");
  formatter.interior(openBrace, closeBrace)
      .slice(1, 3)
      .prepend(FormattingApiProbe::oneSpaceAction());

  ASSERT_EQ(selected.size(), 2u);
  EXPECT_EQ(selected[0], "second:U");
  EXPECT_EQ(selected[1], "third:V");
}

TEST(AbstractFormatterDslTest, HiddenSelectionsReturnExpectedNodes) {
  auto parsed = open_mini_document(
      "entity Alpha { /*one*/ many first:T /*two*/ second:U }");
  ASSERT_NE(parsed.document, nullptr);
  const auto *model = model_of(*parsed.document);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->items.size(), 1u);
  const auto &item = *model->items.front();

  std::vector<std::string> selected;
  FormattingBuilder builder([&selected](const CstNodeView &node, FormattingMode,
                                        const FormattingAction &) {
    selected.emplace_back(node.getText());
  });

  auto formatter = builder.getNodeFormatter(&item);
  formatter.hidden("ML_COMMENT").prepend(FormattingApiProbe::oneSpaceAction());
  formatter.hiddens("ML_COMMENT").prepend(FormattingApiProbe::oneSpaceAction());

  ASSERT_EQ(selected.size(), 3u);
  EXPECT_EQ(selected[0], "/*one*/");
  EXPECT_EQ(selected[1], "/*one*/");
  EXPECT_EQ(selected[2], "/*two*/");
}

TEST(AbstractFormatterTest, OnTypeFormattingOnlyProducesEditsOnCurrentLine) {
  auto parsed = open_mini_document("entity Alpha {}\nentity    Beta {}");
  ASSERT_NE(parsed.document, nullptr);

  KeywordSpacingFormatter provider(lookup_services(*parsed.shared, "mini"));
  ::lsp::DocumentOnTypeFormattingParams params{};
  params.position.line = 1;
  params.position.character = 8;

  const auto edits =
      provider.formatDocumentOnType(*parsed.document, params,
                                    utils::default_cancel_token);
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits.front().range.start.line, 1u);
  EXPECT_EQ(edits.front().newText, " ");
}

TEST(AbstractFormatterTest, KeywordFormattingMatchesCaseInsensitiveLiterals) {
  auto parsed = open_mini_document("ENTITY    Alpha {}");
  ASSERT_NE(parsed.document, nullptr);

  KeywordSpacingFormatter provider(lookup_services(*parsed.shared, "mini"));
  ::lsp::DocumentFormattingParams params{};

  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(apply_text_edits(*parsed.document, edits), "ENTITY Alpha {}");
}

TEST(AbstractFormatterTest, HigherPriorityFormattingWinsForSameRegion) {
  ::lsp::DocumentFormattingParams params{};
  auto parsed = open_mini_document("entity   Alpha {}");
  ASSERT_NE(parsed.document, nullptr);

  PriorityFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(apply_text_edits(*parsed.document, edits), "entity Alpha {}");
}

TEST(AbstractFormatterTest, OverlappingEditsKeepTheLaterSpecificFormatting) {
  ::lsp::DocumentFormattingParams params{};
  auto parsed = open_mini_document("entity   Alpha {}");
  ASSERT_NE(parsed.document, nullptr);

  OverlapFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(apply_text_edits(*parsed.document, edits), "entity Alpha {}");
}

TEST(AbstractFormatterTest, ZeroWidthOverlappingEditsCollapseToOneInsertion) {
  ::lsp::DocumentFormattingParams params{};
  auto parsed = open_mini_document("entity X {a:b}");
  ASSERT_NE(parsed.document, nullptr);

  AdjacentGapFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  // The name's append and the colon's prepend hit the same zero-width gap;
  // exactly one space must be inserted, not two.
  EXPECT_EQ(apply_text_edits(*parsed.document, edits), "entity X {a :b}");
}

TEST(AbstractFormatterTest, AllowMoreAndAllowLessCanSuppressEdits) {
  ::lsp::DocumentFormattingParams params{};
  auto parsed = open_mini_document("entity    Alpha{}");
  ASSERT_NE(parsed.document, nullptr);

  FlexibleWhitespaceFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  EXPECT_TRUE(edits.empty());
}

TEST(AbstractFormatterTest, FitChoosesSingleLineFormattingWhenApplicable) {
  ::lsp::DocumentFormattingParams params{};
  auto parsed = open_mini_document("entity Alpha   {}");
  ASSERT_NE(parsed.document, nullptr);

  FitFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(apply_text_edits(*parsed.document, edits), "entity Alpha {}");
}

TEST(AbstractFormatterTest, PreservesCommentsAndOnlyReindentsHiddenNodes) {
  auto parsed = open_mini_document(
      "entity Alpha {\n"
      "// note\n"
      "many first:T\n"
      "second:U\n"
      "}");
  ASSERT_NE(parsed.document, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;

  MiniLayoutFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*parsed.document, edits),
            "entity Alpha {\n"
            "  // note\n"
            "  many first: T\n"
            "  second: U\n"
            "}");
}

TEST(AbstractFormatterTest, PreservesTrailingCommentOnSameLineAsPreviousNode) {
  // The `// note` sits on the same line as the `T` it trails; formatting must
  // not reflow its leading gap (that would push it onto its own line) nor
  // reindent it. The surrounding fields are still reformatted normally.
  auto parsed = open_mini_document(
      "entity Alpha {\n"
      "many first:T // note\n"
      "second:U\n"
      "}");
  ASSERT_NE(parsed.document, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;

  MiniLayoutFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*parsed.document, edits),
            "entity Alpha {\n"
            "  many first: T // note\n"
            "  second: U\n"
            "}");
}

TEST(AbstractFormatterTest, ReindentsColumnZeroMultilineComment) {
  // A multi-line comment starting at column 0 is both reindented on its first
  // line and reflowed on its continuation lines. The first-line indent has an
  // empty [line start, comment) range, so it must fold into the comment-text
  // replacement rather than emit a zero-width insertion that races the
  // replacement at the same offset.
  auto parsed = open_mini_document(
      "entity Alpha {\n"
      "/* a\n"
      "b */\n"
      "many first:T\n"
      "}");
  ASSERT_NE(parsed.document, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;

  MiniLayoutFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*parsed.document, edits),
            "entity Alpha {\n"
            "  /* a\n"
            "  b */\n"
            "  many first: T\n"
            "}");
}

TEST(AbstractFormatterTest, HiddenSelectionsCanFormatCommentsInScopedSubtrees) {
  auto parsed = open_mini_document(
      "entity Alpha {\n"
      "// note\n"
      "many first:T\n"
      "}");
  ASSERT_NE(parsed.document, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;

  HiddenSelectionFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*parsed.document, edits),
            "entity Alpha {\n"
            "  // note\n"
            "  many first: T\n"
            "}");
}

TEST(AbstractFormatterTest, HiddenNodeCallbacksCanReplaceCommentText) {
  auto parsed = open_mini_document(
      "entity Alpha {\n"
      "/*old\n"
      "line*/\n"
      "many first:T\n"
      "}");
  ASSERT_NE(parsed.document, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;

  HiddenReplacementFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*parsed.document, edits),
            "entity Alpha {\n"
            "  /* level 1\n"
            "   * updated\n"
            "   */\n"
            "  many first: T\n"
            "}");
}

TEST(AbstractFormatterTest, MultilineCommentUtilityFormatsAccordingToOptions) {
  struct Case {
    const char *name;
    std::string_view input;
    MultilineCommentFormatOptions options;
    std::string_view expected;
  };
  static const Case kCases[] = {
      // NormalizesTagsAndContinuation: default options collapse whitespace,
      // normalize tag spacing, and indent tag continuation lines.
      {.name = "NormalizesTagsAndContinuation",
       .input = "/**\n"
                "*   Adds   numbers.\n"
                "* @param   x   first   value\n"
                "*   more   details\n"
                "*\n"
                "*/",
       .options = {},
       .expected = "/**\n"
                   " * Adds numbers.\n"
                   " * @param x first value\n"
                   " *  more details\n"
                   " */"},
      // UsesConfiguredTagPrefix: a custom tagStart ("!") drives tag handling.
      {.name = "UsesConfiguredTagPrefix",
       .input = "/**\n"
                "* !param   x   first   value\n"
                "*   more   details\n"
                "*/",
       .options = {.tagStart = std::string("!")},
       .expected = "/**\n"
                   " * !param x first value\n"
                   " *  more details\n"
                   " */"},
      // UsesConfiguredLinePrefix: custom start/end/newLineStart tokens.
      {.name = "UsesConfiguredLinePrefix",
       .input = "/*\n"
                "#   hello\n"
                "#   world\n"
                "*/",
       .options = {.start = "/*", .end = "*/", .newLineStart = " #"},
       .expected = "/*\n"
                   " # hello\n"
                   " # world\n"
                   " */"},
      // KeepsFirstLineUntouchedWhenMultiline: content right after the start
      // token is preserved on its own continuation line.
      {.name = "KeepsFirstLineUntouchedWhenMultiline",
       .input = "/*#   hello\n"
                "#   world\n"
                "*/",
       .options = {.start = "/*", .end = "*/", .newLineStart = " #"},
       .expected = "/*\n"
                   " # # hello\n"
                   " # world\n"
                   " */"},
      // KeepsWhitespaceOnlyPrefixesForClosingLine: a whitespace-only
      // newLineStart is applied to content and the closing line.
      {.name = "KeepsWhitespaceOnlyPrefixesForClosingLine",
       .input = "/*\n"
                "comment\n"
                "*/",
       .options = {.start = "/*", .end = "*/", .newLineStart = "    "},
       .expected = "/*\n"
                   "    comment\n"
                   "    */"},
      // LeavesUnmatchedCommentsUntouched: text not matching start/end is
      // returned verbatim.
      {.name = "LeavesUnmatchedCommentsUntouched",
       .input = "/* keep me */",
       .options = {},
       .expected = "/* keep me */"},
  };

  for (const auto &c : kCases) {
    SCOPED_TRACE(c.name);
    EXPECT_EQ(FormattingApiProbe::formatComment(c.input, c.options), c.expected);
  }
}

TEST(AbstractFormatterTest, LineCommentUtilityNormalizesSpacing) {
  EXPECT_EQ(FormattingApiProbe::formatLine("//   hello   world"),
            "// hello world");
}

TEST(AbstractFormatterTest, PreservesLeadingHiddenNodesBeforeRootContent) {
  auto parsed = open_mini_document(
      "    /** note */\n"
      "   entity Alpha {}");
  ASSERT_NE(parsed.document, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;

  MiniLayoutFormatter provider(lookup_services(*parsed.shared, "mini"));
  const auto edits =
      provider.formatDocument(*parsed.document, params,
                              utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*parsed.document, edits),
            "/** note */\n"
            "entity Alpha {}");
}

} // namespace
} // namespace pegium
