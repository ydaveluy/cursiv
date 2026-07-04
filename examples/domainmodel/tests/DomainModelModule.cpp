#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>

#include <domainmodel/cli/CliUtils.hpp>
#include <domainmodel/cli/Generator.hpp>
#include <domainmodel/lsp/LspModule.hpp>
#include <domainmodel/lsp/LspServices.hpp>

#include <domainmodel/lsp/DomainModelFormatter.hpp>
#include <domainmodel/lsp/DomainModelRenameProvider.hpp>
#include <domainmodel/core/references/DomainModelScopeComputation.hpp>
#include <domainmodel/core/references/QualifiedNameProvider.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/cli/CliUtils.hpp>
#include <pegium/core/references/DefaultNameProvider.hpp>
#include <pegium/lsp/services/LanguageServerFeatures.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/core/utils/UriUtils.hpp>

namespace domainmodel::tests {
namespace {
std::filesystem::path example_root() {
  return pegium::test::current_source_directory().parent_path() / "example";
}

std::filesystem::path make_temp_directory() {
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto path = std::filesystem::temp_directory_path() /
              ("pegium-domainmodel-tests-" + suffix);
  std::filesystem::create_directories(path);
  return path;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::string expected_e2_java() {
  return "package qualifiednames.foo.bar;\n\n"
         "class E2 extends E1 {\n"
         "    private E2 next;\n"
         "    private baz.E3 other;\n"
         "    private baz.nested.E5 nested;\n"
         "    private big.Int time;\n\n"
         "    public void setNext(E2 next) {\n"
         "        this.next = next;\n"
         "    }\n\n"
         "    public E2 getNext() {\n"
         "        return next;\n"
         "    }\n\n"
         "    public void setOther(baz.E3 other) {\n"
         "        this.other = other;\n"
         "    }\n\n"
         "    public baz.E3 getOther() {\n"
         "        return other;\n"
         "    }\n\n"
         "    public void setNested(baz.nested.E5 nested) {\n"
         "        this.nested = nested;\n"
         "    }\n\n"
         "    public baz.nested.E5 getNested() {\n"
         "        return nested;\n"
         "    }\n\n"
         "    public void setTime(big.Int time) {\n"
         "        this.time = time;\n"
         "    }\n\n"
         "    public big.Int getTime() {\n"
         "        return time;\n"
         "    }\n"
         "}\n";
}

std::vector<std::string>
collect_description_names(
    const std::vector<pegium::workspace::AstNodeDescription> &descriptions) {
  std::vector<std::string> names;
  names.reserve(descriptions.size());
  for (const auto &description : descriptions) {
    names.push_back(description.name);
  }
  std::ranges::sort(names);
  return names;
}

std::vector<std::string>
collect_local_symbol_names(const pegium::workspace::LocalSymbols &symbols,
                           const pegium::AstNode *container) {
  std::vector<std::string> names;
  if (const auto *entries = symbols.forContainer(container)) {
    for (const auto &bucket : *entries) {
      for (const auto &description : bucket.ownedEntries) {
        names.push_back(description.name);
      }
    }
  }
  std::ranges::sort(names);
  return names;
}

std::string apply_text_edits(const pegium::workspace::Document &document,
                             std::vector<::lsp::TextEdit> edits) {
  const auto &textDocument = document.textDocument();
  auto text = std::string(textDocument.getText());
  std::ranges::sort(edits, [&textDocument](const auto &left, const auto &right) {
    return textDocument.offsetAt(left.range.start) >
           textDocument.offsetAt(right.range.start);
  });
  for (const auto &edit : edits) {
    const auto begin = textDocument.offsetAt(edit.range.start);
    const auto end = textDocument.offsetAt(edit.range.end);
    text.replace(begin, end - begin, edit.newText);
  }
  return text;
}

pegium::text::Position position_of(const pegium::workspace::Document &document,
                                   std::string_view needle) {
  const auto &textDocument = document.textDocument();
  const auto offset = textDocument.getText().find(needle);
  EXPECT_NE(offset, std::string::npos);
  return textDocument.positionAt(
      static_cast<pegium::TextOffset>(offset == std::string::npos ? 0 : offset));
}

std::vector<std::string> collect_reference_ranges(
    const pegium::workspace::Documents &documents,
    const std::vector<pegium::workspace::ReferenceDescription> &references) {
  std::vector<std::string> ranges;
  for (const auto &reference : references) {
    const auto document = documents.getDocument(reference.sourceDocumentId);
    if (document == nullptr) {
      continue;
    }
    const auto begin =
        document->textDocument().positionAt(reference.sourceOffset);
    const auto end = document->textDocument().positionAt(
        static_cast<pegium::TextOffset>(reference.sourceOffset +
                                        reference.sourceLength));
    ranges.push_back(std::to_string(begin.line) + ":" +
                     std::to_string(begin.character) + "->" +
                     std::to_string(end.line) + ":" +
                     std::to_string(end.character));
  }
  std::ranges::sort(ranges);
  return ranges;
}

TEST(DomainModelModuleTest, InstallsLanguageSpecificOverrides) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services =
      domainmodel::createDomainModelLspServices(*shared, "domain-model");

  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->qualifiedNameProvider, nullptr);
  ASSERT_NE(services->validator, nullptr);
  EXPECT_NE(dynamic_cast<pegium::references::DefaultNameProvider *>(
                services->references.nameProvider.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<domainmodel::references::DomainModelScopeComputation *>(
                services->references.scopeComputation.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<domainmodel::DomainModelRenameProvider *>(
                services->lsp.renameProvider.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<domainmodel::DomainModelFormatter *>(
                services->lsp.formatter.get()),
            nullptr);

  static_assert(std::is_base_of_v<pegium::NamedAstNode, domainmodel::ast::Entity>);
  domainmodel::ast::Entity entity;
  entity.name = "Person";
  EXPECT_EQ(services->references.nameProvider->getName(entity),
            (std::optional<std::string>{"Person"}));

  domainmodel::references::QualifiedNameProvider qualifiedNames;
  domainmodel::ast::PackageDeclaration parent;
  parent.name = "blog";
  domainmodel::ast::PackageDeclaration child;
  child.name = "internal";
  child.setContainer(parent);
  EXPECT_EQ(qualifiedNames.getQualifiedName(child, "Author"), "blog.internal.Author");
}

TEST(DomainModelModuleTest, ValidatorWarnsOnLowerCaseTypeName) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("domainmodel-module.dmodel"),
      "domain-model", "entity person {}");

  ASSERT_NE(document, nullptr);
  EXPECT_TRUE(
      pegium::test::has_diagnostic_message(*document, "Type name should start"));
}

TEST(DomainModelModuleTest, ValidatorWarnsOnLowerCaseDataTypeName) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("domainmodel-datatype.dmodel"),
      "domain-model", "datatype string");

  ASSERT_NE(document, nullptr);
  EXPECT_TRUE(
      pegium::test::has_diagnostic_message(*document, "Type name should start"));
}

TEST(DomainModelModuleTest, RegistersExampleSpecificServicesInRegistry) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  const auto *coreServices =
      &shared->serviceRegistry->getServices(
          pegium::test::make_file_uri("registry-check.dmodel"));
  const auto *lspServices = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(lspServices, nullptr);
  const auto *services =
      dynamic_cast<const domainmodel::DomainModelServices *>(lspServices);
  ASSERT_NE(services, nullptr);
  EXPECT_NE(services->qualifiedNameProvider, nullptr);
  EXPECT_NE(services->validator, nullptr);
}

TEST(DomainModelModuleTest, ScopeComputationQualifiesExportsAndLocalSymbols) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("scope-names.dmodel"), "domain-model",
      "package blog {\n"
      "  datatype String\n"
      "  package internal {\n"
      "    entity User {}\n"
      "  }\n"
      "}\n");
  ASSERT_NE(document, nullptr);

  const auto &services = shared->serviceRegistry->getServices(document->uri);
  ASSERT_NE(services.references.scopeComputation, nullptr);

  auto *model =
      dynamic_cast<domainmodel::ast::DomainModel *>(document->parseResult.value);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->elements.size(), 1u);

  auto *blog =
      dynamic_cast<domainmodel::ast::PackageDeclaration *>(model->elements[0]);
  ASSERT_NE(blog, nullptr);
  ASSERT_EQ(blog->elements.size(), 2u);

  auto *internal = dynamic_cast<domainmodel::ast::PackageDeclaration *>(
      blog->elements[1]);
  ASSERT_NE(internal, nullptr);

  const auto exports =
      services.references.scopeComputation->collectExportedSymbols(*document, {});
  EXPECT_EQ(collect_description_names(exports),
            (std::vector<std::string>{"blog.String", "blog.internal.User"}));

  const auto locals =
      services.references.scopeComputation->collectLocalSymbols(*document, {});
  EXPECT_EQ(collect_local_symbol_names(locals, model),
            (std::vector<std::string>{"blog.String", "blog.internal.User"}));
  EXPECT_EQ(collect_local_symbol_names(locals, blog),
            (std::vector<std::string>{"String", "internal.User"}));
  EXPECT_EQ(collect_local_symbol_names(locals, internal),
            (std::vector<std::string>{"User"}));
}

TEST(DomainModelModuleTest, FindsCrossReferencesFromDeclarations) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  auto datatypeDocument = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("datatypes.dmodel"), "domain-model",
      "\n"
      "datatype String\n"
      "datatype Int\n"
      "datatype Decimal\n"
      "\n"
      "package big {\n"
      "    datatype Int\n"
      "    datatype Decimal\n"
      "}\n");
  ASSERT_NE(datatypeDocument, nullptr);

  auto referencingDocument = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("referencing.dmodel"), "domain-model",
      "\n"
      " entity Blog {\n"
      "    title: String\n"
      "    description: String\n"
      "    time: big.Int\n"
      "}\n");
  ASSERT_NE(referencingDocument, nullptr);

  const auto &services =
      shared->serviceRegistry->getServices(datatypeDocument->uri);
  ASSERT_NE(services.references.references, nullptr);

  auto *model = dynamic_cast<domainmodel::ast::DomainModel *>(
      datatypeDocument->parseResult.value);
  ASSERT_NE(model, nullptr);
  ASSERT_FALSE(model->elements.empty());

  const auto references = collect_reference_ranges(
      *shared->workspace.documents,
      services.references.references->findReferences(*model->elements[0], {}));
  EXPECT_EQ(references, (std::vector<std::string>{"2:11->2:17", "3:17->3:23"}));
}

TEST(DomainModelModuleTest, ReindexesCrossReferencesAfterDocumentUpdate) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  auto superDocument = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("super.dmodel"), "domain-model",
      "entity NoSuperEntity {}");
  ASSERT_NE(superDocument, nullptr);

  auto extendingDocument = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("extends.dmodel"), "domain-model",
      "entity SomeEntity extends SuperEntity {}");
  ASSERT_NE(extendingDocument, nullptr);

  const auto &services = shared->serviceRegistry->getServices(superDocument->uri);
  ASSERT_NE(services.references.references, nullptr);

  auto *initialModel = dynamic_cast<domainmodel::ast::DomainModel *>(
      superDocument->parseResult.value);
  ASSERT_NE(initialModel, nullptr);
  ASSERT_FALSE(initialModel->elements.empty());
  EXPECT_TRUE(services.references.references->findReferences(
                  *initialModel->elements[0], {})
                  .empty());

  ASSERT_NE(pegium::test::set_text_document(
                *shared->lsp.textDocuments, superDocument->uri, "domain-model",
                "entity SuperEntity {}", 2),
            nullptr);
  const std::array changedDocumentIds{
      shared->workspace.documents->getOrCreateDocumentId(superDocument->uri)};
  (void)shared->workspace.documentBuilder->update(changedDocumentIds, {});

  auto updatedSuperDocument =
      shared->workspace.documents->getDocument(superDocument->uri);
  ASSERT_NE(updatedSuperDocument, nullptr);
  auto *updatedModel = dynamic_cast<domainmodel::ast::DomainModel *>(
      updatedSuperDocument->parseResult.value);
  ASSERT_NE(updatedModel, nullptr);
  ASSERT_FALSE(updatedModel->elements.empty());

  const auto updatedReferences =
      services.references.references->findReferences(*updatedModel->elements[0], {});
  ASSERT_EQ(updatedReferences.size(), 1u);
  const auto referenceText = updatedReferences[0].sourceText(
      extendingDocument->textDocument().getText());
  EXPECT_EQ(referenceText, "SuperEntity");
}

TEST(DomainModelModuleTest,
     RenameUpdatesQualifiedReferencesThroughPackageSubtree) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  auto typesDocument = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("rename-types.dmodel"), "domain-model",
      "package big {\n"
      "  datatype Int\n"
      "  package inner {\n"
      "    entity User {}\n"
      "  }\n"
      "}\n");
  ASSERT_NE(typesDocument, nullptr);

  auto referencesDocument = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("rename-refs.dmodel"), "domain-model",
      "entity Blog {\n"
      "  size: big.Int\n"
      "  owner: big.inner.User\n"
      "}\n");
  ASSERT_NE(referencesDocument, nullptr);

  ::lsp::RenameParams params{};
  params.textDocument.uri =
      ::lsp::DocumentUri(::lsp::Uri::parse(typesDocument->uri));
  params.position = position_of(*typesDocument, "big");
  params.newName = "large";

  const auto edit = pegium::rename(*shared, params);
  ASSERT_TRUE(edit.has_value());
  ASSERT_TRUE(edit->changes.has_value());

  const auto typesUri = ::lsp::DocumentUri(::lsp::Uri::parse(typesDocument->uri));
  const auto refsUri =
      ::lsp::DocumentUri(::lsp::Uri::parse(referencesDocument->uri));
  const auto typesEditsIt = edit->changes->find(typesUri);
  const auto refsEditsIt = edit->changes->find(refsUri);
  ASSERT_NE(typesEditsIt, edit->changes->end());
  ASSERT_NE(refsEditsIt, edit->changes->end());

  EXPECT_EQ(
      apply_text_edits(*typesDocument, typesEditsIt->second),
      "package large {\n"
      "  datatype Int\n"
      "  package inner {\n"
      "    entity User {}\n"
      "  }\n"
      "}\n");
  EXPECT_EQ(
      apply_text_edits(*referencesDocument, refsEditsIt->second),
      "entity Blog {\n"
      "  size: large.Int\n"
      "  owner: large.inner.User\n"
      "}\n");
}

TEST(DomainModelModuleTest, RenameFeatureEditsOnlyFeatureDeclaration) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(domainmodel::registerDomainModelLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("rename-feature.dmodel"), "domain-model",
      "datatype String\n"
      "package blog {\n"
      "  entity Post {\n"
      "    title: String\n"
      "  }\n"
      "}\n");
  ASSERT_NE(document, nullptr);

  ::lsp::RenameParams params{};
  params.textDocument.uri =
      ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  params.position = position_of(*document, "title");
  params.newName = "headline";

  const auto edit = pegium::rename(*shared, params);
  ASSERT_TRUE(edit.has_value());
  ASSERT_TRUE(edit->changes.has_value());

  const auto uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  const auto editsIt = edit->changes->find(uri);
  ASSERT_NE(editsIt, edit->changes->end());
  ASSERT_EQ(editsIt->second.size(), 1u);
  EXPECT_EQ(apply_text_edits(*document, editsIt->second),
            "datatype String\n"
            "package blog {\n"
            "  entity Post {\n"
            "    headline: String\n"
            "  }\n"
            "}\n");
}

TEST(DomainModelModuleTest, FormatterFormatsDocument) {
  struct Case {
    const char *name;
    const char *uri;
    const char *source;
    int tabSize;
    const char *expected;
  };
  static const Case cases[] = {
      {"FormatsCompactModel", "formatting.dmodel",
       "package foo.bar { datatype Complex entity E2 extends E1 { next: E2 other: Complex }}",
       4,
       "package foo.bar {\n"
       "    datatype Complex\n"
       "    entity E2 extends E1 {\n"
       "        next: E2\n"
       "        other: Complex\n"
       "    }\n"
       "}"},
      {"PreservesCommentsWhileReindenting", "formatting-comments.dmodel",
       "package foo.bar {\n"
       "// package comment\n"
       "entity E1 {\n"
       "// feature comment\n"
       "next:E1\n"
       "}\n"
       "}",
       2,
       "package foo.bar {\n"
       "  // package comment\n"
       "  entity E1 {\n"
       "    // feature comment\n"
       "    next: E1\n"
       "  }\n"
       "}"},
      {"IndentsMultilineCommentsInsideBlocks",
       "formatting-multiline-comments.dmodel",
       "package big {\n"
       "    /*\n"
       "comment\n"
       "*/\n"
       "    datatype Int\n"
       "}",
       4,
       "package big {\n"
       "    /*\n"
       "    comment\n"
       "    */\n"
       "    datatype Int\n"
       "}"},
      {"PreservesDocCommentStyle", "formatting-doc-comments.dmodel",
       "package big {\n"
       "/**   comment */\n"
       "datatype Int\n"
       "}",
       4,
       "package big {\n"
       "    /** comment */\n"
       "    datatype Int\n"
       "}"},
  };

  for (const auto &c : cases) {
    SCOPED_TRACE(c.name);
    auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
    pegium::installDefaultSharedCoreServices(*shared);
    pegium::installDefaultSharedLspServices(*shared);
    pegium::test::initialize_shared_workspace_for_tests(*shared);
    auto registeredServices =
        domainmodel::createDomainModelLspServices(*shared, "domain-model");

    ASSERT_NE(registeredServices, nullptr);

    shared->serviceRegistry->registerServices(std::move(registeredServices));
    auto document = pegium::test::open_and_build_document(
        *shared, pegium::test::make_file_uri(c.uri), "domain-model", c.source);
    ASSERT_NE(document, nullptr);
    const auto *coreServices =
        &shared->serviceRegistry->getServices(document->uri);
    const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
    ASSERT_NE(services, nullptr);
    ASSERT_NE(services->lsp.formatter, nullptr);

    ::lsp::DocumentFormattingParams params{};
    params.options.insertSpaces = true;
    params.options.tabSize = c.tabSize;
    const auto edits = services->lsp.formatter->formatDocument(
        *document, params, pegium::utils::default_cancel_token);

    ASSERT_FALSE(edits.empty());
    EXPECT_EQ(apply_text_edits(*document, edits), c.expected);
  }
}

TEST(DomainModelModuleTest, GeneratorCreatesExpectedJavaFiles) {
  auto shared = std::make_unique<domainmodel::DomainModelSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services = domainmodel::createDomainModelLspServices(*shared);
  auto &domainmodelServices = *services;
  shared->serviceRegistry->registerServices(std::move(services));

  const auto inputPath = example_root() / "qualified-names.dmodel";
  const auto absoluteInputPath = std::filesystem::absolute(inputPath);
  domainmodel::set_root_folder(absoluteInputPath.string(),
                                    domainmodelServices,
                                    std::filesystem::absolute(example_root()).string());
  auto document = pegium::build_document_from_path(
      absoluteInputPath.string(), domainmodelServices);
  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->diagnostics.empty());

  const auto &model = domainmodel::extract_ast_node(*document);

  const auto tempDirectory = make_temp_directory();
  const auto generatedDir = domainmodel::generate_java(
      model, inputPath.string(), tempDirectory.string());
  const auto rootDir = std::filesystem::path(generatedDir);
  EXPECT_TRUE(std::filesystem::exists(rootDir / "E1.java"));
  EXPECT_TRUE(std::filesystem::exists(rootDir / "foo" / "bar" / "E2.java"));
  EXPECT_TRUE(std::filesystem::exists(rootDir / "baz" / "E3.java"));
  EXPECT_TRUE(std::filesystem::exists(rootDir / "baz" / "nested" / "E5.java"));

  const auto e2Content = read_file(rootDir / "foo" / "bar" / "E2.java");
  EXPECT_EQ(e2Content, expected_e2_java());

  std::filesystem::remove_all(tempDirectory);
}

TEST(DomainModelModuleTest, CliGenerateCreatesExpectedJavaTree) {
  const auto tempDirectory = make_temp_directory();
  const auto outputDirectory = tempDirectory / "generated-output";
  const auto inputPath =
      std::filesystem::absolute(example_root() / "qualified-names.dmodel");
  const std::string command =
      std::string("\"") + PEGIUM_EXAMPLE_DOMAINMODEL_CLI_PATH +
      "\" generate \"" + inputPath.string() + "\" -d \"" +
      outputDirectory.string() + "\" -q";

  const auto exitCode = std::system(command.c_str());
  ASSERT_EQ(exitCode, 0);

  const auto rootDir = outputDirectory / "qualifiednames";
  EXPECT_TRUE(std::filesystem::exists(rootDir / "E1.java"));
  EXPECT_TRUE(std::filesystem::exists(rootDir / "foo" / "bar" / "E2.java"));
  EXPECT_TRUE(std::filesystem::exists(rootDir / "baz" / "E3.java"));
  EXPECT_TRUE(std::filesystem::exists(rootDir / "baz" / "nested" / "E5.java"));
  EXPECT_EQ(read_file(rootDir / "foo" / "bar" / "E2.java"), expected_e2_java());

  std::filesystem::remove_all(tempDirectory);
}

TEST(DomainModelModuleTest, CliGenerateAcceptsRelativeRoot) {
  const auto tempDirectory = make_temp_directory();
  const auto outputDirectory = tempDirectory / "generated-output";
  const auto inputPath =
      std::filesystem::absolute(example_root() / "qualified-names.dmodel");
  const auto repoRoot =
      example_root().parent_path().parent_path().parent_path();
  const auto relativeRoot =
      std::filesystem::relative(example_root(), repoRoot).string();
  const std::string command =
      std::string("cd \"") + repoRoot.string() + "\" && \"" +
      PEGIUM_EXAMPLE_DOMAINMODEL_CLI_PATH + "\" generate \"" +
      inputPath.string() + "\" -d \"" + outputDirectory.string() + "\" -r \"" +
      relativeRoot + "\" -q";

  const auto exitCode = std::system(command.c_str());
  ASSERT_EQ(exitCode, 0);

  const auto rootDir = outputDirectory / "qualifiednames";
  EXPECT_TRUE(std::filesystem::exists(rootDir / "foo" / "bar" / "E2.java"));
  EXPECT_EQ(read_file(rootDir / "foo" / "bar" / "E2.java"), expected_e2_java());

  std::filesystem::remove_all(tempDirectory);
}

TEST(DomainModelModuleTest, ExtractDestinationAndNameUsesDefaultCliConvention) {
  const auto data = domainmodel::extract_destination_and_name(
      "/tmp/some-dir/qualified-names.dmodel", std::nullopt);
  EXPECT_EQ(data.destination, "generated");
  EXPECT_EQ(data.name, "qualifiednames");

  const auto overridden = domainmodel::extract_destination_and_name(
      "/tmp/some-dir/qualified-names.dmodel", "/tmp/out");
  EXPECT_EQ(overridden.destination, "/tmp/out");
  EXPECT_EQ(overridden.name, "qualifiednames");
}

} // namespace
} // namespace domainmodel::tests
