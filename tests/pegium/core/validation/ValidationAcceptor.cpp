#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <span>
#include <utility>

#include <pegium/core/CoreTestSupport.hpp>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>
#include <pegium/core/workspace/Document.hpp>

namespace pegium::validation {
namespace {

using namespace pegium::parser;

struct ValidationAcceptNode : pegium::AstNode {
  string name;
  vector<string> tags;
};

struct ForeignNode : pegium::AstNode {
  string value;
};

using ValidationAcceptBuilder = ValidationDiagnosticBuilder<ValidationAcceptNode>;

template <typename Builder>
concept SupportsNameProperty = requires(Builder builder) {
  builder.template property<&ValidationAcceptNode::name>();
};

template <typename Builder>
concept SupportsIndexedTagsProperty = requires(Builder builder) {
  builder.template property<&ValidationAcceptNode::tags>(0u);
};

template <typename Builder>
concept SupportsIndexedNameProperty = requires(Builder builder) {
  builder.template property<&ValidationAcceptNode::name>(0u);
};

template <typename Builder>
concept SupportsForeignProperty = requires(Builder builder) {
  builder.template property<&ForeignNode::value>();
};

static_assert(SupportsNameProperty<ValidationAcceptBuilder>);
static_assert(SupportsIndexedTagsProperty<ValidationAcceptBuilder>);
static_assert(!SupportsIndexedNameProperty<ValidationAcceptBuilder>);
static_assert(!SupportsForeignProperty<ValidationAcceptBuilder>);

ValidationAcceptNode &parse_validation_accept_node(workspace::Document &document) {
  static const ParserRule<ValidationAcceptNode> root{
      "Root", assign<&ValidationAcceptNode::name>("alpha"_kw) + ":"_kw +
                  append<&ValidationAcceptNode::tags>("one"_kw) + ","_kw +
                  append<&ValidationAcceptNode::tags>("two"_kw)};

  pegium::test::parse_rule(root, document, SkipperBuilder().build());
  auto *node = dynamic_cast<ValidationAcceptNode *>(document.parseResult.value);
  EXPECT_NE(node, nullptr);
  return *node;
}

TEST(ValidationAcceptorTest, EmitsDiagnosticForSelectedPropertySubrange) {
  workspace::Document document(
      test::make_text_document("file:///validation-acceptor.pg", "mini",
                               "alpha:one,two"));
  auto &node = parse_validation_accept_node(document);

  pegium::Diagnostic diagnostic;
  auto acceptorCb = 
      [&diagnostic](pegium::Diagnostic value) {
        diagnostic = std::move(value);      };

  const ValidationAcceptor acceptor{ValidationAcceptor::Callback(acceptorCb)};

  acceptor.error(node, "Invalid name")
      .property<&ValidationAcceptNode::name>()
      .range(1, 4)
      .code("7", "https://example.invalid/validation/name")
      .tags({pegium::DiagnosticTag::Unnecessary,
             pegium::DiagnosticTag::Deprecated})
      .relatedInformation(node, "Defined here")
      .data(pegium::JsonValue(7));

  EXPECT_EQ(diagnostic.severity, pegium::DiagnosticSeverity::Error);
  EXPECT_EQ(diagnostic.message, "Invalid name");
  EXPECT_EQ(diagnostic.begin, 1u);
  EXPECT_EQ(diagnostic.end, 4u);
  ASSERT_TRUE(diagnostic.code.has_value());
  ASSERT_TRUE(std::holds_alternative<std::string>(*diagnostic.code));
  EXPECT_EQ(std::get<std::string>(*diagnostic.code), "7");
  ASSERT_TRUE(diagnostic.codeDescription.has_value());
  EXPECT_EQ(*diagnostic.codeDescription,
            "https://example.invalid/validation/name");
  ASSERT_EQ(diagnostic.tags.size(), 2u);
  EXPECT_EQ(diagnostic.tags[0], pegium::DiagnosticTag::Unnecessary);
  EXPECT_EQ(diagnostic.tags[1], pegium::DiagnosticTag::Deprecated);
  ASSERT_EQ(diagnostic.relatedInformation.size(), 1u);
  EXPECT_EQ(diagnostic.relatedInformation[0].message, "Defined here");
  EXPECT_EQ(diagnostic.relatedInformation[0].begin, 0u);
  EXPECT_EQ(diagnostic.relatedInformation[0].end, 13u);
  ASSERT_TRUE(diagnostic.data.has_value());
  EXPECT_TRUE(diagnostic.data->isInteger());
  EXPECT_EQ(diagnostic.data->integer(), 7);
}

TEST(ValidationAcceptorTest, SelectsIndexedVectorPropertyAssignment) {
  workspace::Document document(
      test::make_text_document("file:///validation-acceptor.pg", "mini",
                               "alpha:one,two"));
  auto &node = parse_validation_accept_node(document);

  pegium::Diagnostic diagnostic;
  auto acceptorCb = 
      [&diagnostic](pegium::Diagnostic value) {
        diagnostic = std::move(value);      };

  const ValidationAcceptor acceptor{ValidationAcceptor::Callback(acceptorCb)};

  acceptor.warning(node, "Second tag")
      .property<&ValidationAcceptNode::tags>(1u);

  EXPECT_EQ(diagnostic.severity, pegium::DiagnosticSeverity::Warning);
  EXPECT_EQ(diagnostic.message, "Second tag");
  EXPECT_EQ(diagnostic.begin, 10u);
  EXPECT_EQ(diagnostic.end, 13u);
}

TEST(ValidationAcceptorTest, AnchorsDiagnosticOnKeyword) {
  workspace::Document document(
      test::make_text_document("file:///validation-acceptor.pg", "mini",
                               "alpha:one,two"));
  auto &node = parse_validation_accept_node(document);

  pegium::Diagnostic diagnostic;
  auto acceptorCb = [&diagnostic](pegium::Diagnostic value) {
    diagnostic = std::move(value);
  };

  const ValidationAcceptor acceptor{ValidationAcceptor::Callback(acceptorCb)};

  // The ":" keyword sits at offsets 5..6 in "alpha:one,two".
  acceptor.error(node, "Bad separator").keyword(":");

  EXPECT_EQ(diagnostic.severity, pegium::DiagnosticSeverity::Error);
  EXPECT_EQ(diagnostic.message, "Bad separator");
  EXPECT_EQ(diagnostic.begin, 5u);
  EXPECT_EQ(diagnostic.end, 6u);
}

TEST(ValidationAcceptorTest, KeepsWholeNodeRangeForAbsentKeyword) {
  workspace::Document document(
      test::make_text_document("file:///validation-acceptor.pg", "mini",
                               "alpha:one,two"));
  auto &node = parse_validation_accept_node(document);

  pegium::Diagnostic diagnostic;
  auto acceptorCb = [&diagnostic](pegium::Diagnostic value) {
    diagnostic = std::move(value);
  };

  const ValidationAcceptor acceptor{ValidationAcceptor::Callback(acceptorCb)};

  // A keyword that never appears falls back to the whole node's range.
  acceptor.warning(node, "No such keyword").keyword("missing");

  EXPECT_EQ(diagnostic.begin, 0u);
  EXPECT_EQ(diagnostic.end, 13u);
}

TEST(ValidationAcceptorTest, AppendsRelatedInformationFromListAndSpan) {
  workspace::Document document(
      test::make_text_document("file:///validation-acceptor.pg", "mini",
                               "alpha:one,two"));
  auto &node = parse_validation_accept_node(document);

  pegium::Diagnostic diagnostic;
  auto acceptorCb = 
      [&diagnostic](pegium::Diagnostic value) {
        diagnostic = std::move(value);      };

  const ValidationAcceptor acceptor{ValidationAcceptor::Callback(acceptorCb)};

  const std::array<pegium::DiagnosticRelatedInformation, 1> extra{
      pegium::DiagnosticRelatedInformation{
          .uri = "file:///other.pg",
          .message = "Referenced elsewhere",
          .begin = 4,
          .end = 8}};

  acceptor.error(node, "Invalid name")
      .relatedInformation({pegium::DiagnosticRelatedInformation{
          .uri = "file:///first.pg",
          .message = "First entry",
          .begin = 1,
          .end = 2}})
      .relatedInformation(std::span<const pegium::DiagnosticRelatedInformation>(
          extra));

  ASSERT_EQ(diagnostic.relatedInformation.size(), 2u);
  EXPECT_EQ(diagnostic.relatedInformation[0].uri, "file:///first.pg");
  EXPECT_EQ(diagnostic.relatedInformation[0].message, "First entry");
  EXPECT_EQ(diagnostic.relatedInformation[0].begin, 1u);
  EXPECT_EQ(diagnostic.relatedInformation[0].end, 2u);
  EXPECT_EQ(diagnostic.relatedInformation[1].uri, "file:///other.pg");
  EXPECT_EQ(diagnostic.relatedInformation[1].message, "Referenced elsewhere");
  EXPECT_EQ(diagnostic.relatedInformation[1].begin, 4u);
  EXPECT_EQ(diagnostic.relatedInformation[1].end, 8u);
}

} // namespace
} // namespace pegium::validation
