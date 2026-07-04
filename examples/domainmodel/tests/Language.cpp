#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

#include <domainmodel/core/CoreModule.hpp>
#include <domainmodel/lsp/LspServices.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/examples/RecoverySampleTestSupport.hpp>

namespace domainmodel::tests {
namespace {

TEST(DomainModelLanguageTest, ParsesEntityModel) {
  auto parser = createDomainModelParser();
  auto document = pegium::test::parse_document(
      *parser,
      "package blog {\n"
      "  entity Person {}\n"
      "}\n",
      pegium::test::make_file_uri("language.dmodel"), "domain-model");

  ASSERT_TRUE(document->parseSucceeded());
  auto *model = dynamic_cast<ast::DomainModel *>(document->parseResult.value);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->elements.size(), 1u);
}

TEST(DomainModelLanguageTest, ResolvesTypeReferenceToEntitySubtype) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelCoreServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("types.dmodel"), "domain-model",
      "entity Person {}\n"
      "entity Blog {\n"
      "  owner: Person\n"
      "}\n");
  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());

  auto *model =
      dynamic_cast<ast::DomainModel *>(document->parseResult.value);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->elements.size(), 2u);

  auto *blog = dynamic_cast<ast::Entity *>(model->elements[1]);
  ASSERT_NE(blog, nullptr);
  ASSERT_EQ(blog->features.size(), 1u);
  ASSERT_NE(blog->features.front(), nullptr);
  ASSERT_TRUE(blog->features.front()->type);
  const auto *person =
      dynamic_cast<const ast::Entity *>(blog->features.front()->type.get());
  ASSERT_NE(person, nullptr);
  EXPECT_EQ(person->name, "Person");
}

TEST(DomainModelLanguageTest, RejectsSelfExtendingEntityOnTheExtendsKeyword) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelCoreServices(*shared));

  const std::string source = "entity Loop extends Loop {}\n";
  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("self-extends.dmodel"),
      "domain-model", source);
  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());

  const auto *diagnostic = [&]() -> const pegium::Diagnostic * {
    for (const auto &candidate : document->diagnostics) {
      if (candidate.message == "An entity may not extend itself.") {
        return &candidate;
      }
    }
    return nullptr;
  }();
  ASSERT_NE(diagnostic, nullptr);
  EXPECT_EQ(diagnostic->severity, pegium::DiagnosticSeverity::Error);

  // The range underlines the `extends` keyword, not the whole entity.
  const auto keywordBegin = source.find("extends");
  ASSERT_NE(keywordBegin, std::string::npos);
  EXPECT_EQ(diagnostic->begin, keywordBegin);
  EXPECT_EQ(diagnostic->end, keywordBegin + std::string_view("extends").size());
}

TEST(DomainModelLanguageTest,
     RecoversMissingFeatureColonInFirstEntityWithoutDiscardingTheEntity) {
  auto parser = createDomainModelParser();
  auto document = pegium::test::parse_document(
      *parser,
      "entity Post {\n"
      "  many comments Comment\n"
      "  title: String\n"
      "}\n"
      "\n"
      "entity Comment {}\n",
      pegium::test::make_file_uri("missing-many-colon.dmodel"),
      "domain-model");

  const auto &parsed = document->parseResult;
  ASSERT_TRUE(parsed.value);
  EXPECT_TRUE(parsed.fullMatch);
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered);
  EXPECT_FALSE(pegium::test::has_parse_diagnostic_kind(
      parsed.parseDiagnostics, pegium::parser::ParseDiagnosticKind::Incomplete));

  auto *model = dynamic_cast<ast::DomainModel *>(parsed.value);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->elements.size(), 2u);

  auto *post = dynamic_cast<ast::Entity *>(model->elements[0]);
  auto *comment = dynamic_cast<ast::Entity *>(model->elements[1]);
  ASSERT_NE(post, nullptr);
  ASSERT_NE(comment, nullptr);
  EXPECT_EQ(post->name, "Post");
  EXPECT_EQ(comment->name, "Comment");
  ASSERT_EQ(post->features.size(), 2u);
  ASSERT_NE(post->features[0], nullptr);
  EXPECT_TRUE(post->features[0]->many);
  EXPECT_EQ(post->features[0]->name, "comments");
  EXPECT_EQ(post->features[0]->type.getRefText(), "Comment");
}

} // namespace
} // namespace domainmodel::tests
