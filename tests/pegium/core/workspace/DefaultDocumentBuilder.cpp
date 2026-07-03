#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <stop_token>

#include <pegium/core/CoreTestSupport.hpp>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/FeatureValue.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/references/DefaultLinker.hpp>
#include <pegium/core/references/ScopeProvider.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/core/validation/DefaultValidationRegistry.hpp>
#include <pegium/core/workspace/DefaultDocumentBuilder.hpp>

namespace pegium::workspace {
namespace {

using namespace std::chrono_literals;

constexpr std::string_view kDefaultCodeActionsKey = "pegiumDefaultCodeActions";

parser::ExpectPath keyword_expectation(const grammar::Literal &literal) {
  return {
      .elements = {std::addressof(literal)},
  };
}

parser::ExpectPath rule_expectation(const grammar::AbstractRule &rule) {
  return {
      .elements = {std::addressof(rule)},
  };
}

parser::ExpectResult
expectation_result(std::initializer_list<parser::ExpectPath> items) {
  parser::ExpectResult expect;
  expect.frontier.assign(items.begin(), items.end());
  expect.reachedAnchor = !expect.frontier.empty();
  return expect;
}

struct RelinkNode : AstNode {
  std::string name;
};

struct RelinkReferrer : AstNode {
  reference<RelinkNode> node;
};

struct RelinkRoot : AstNode {
  std::vector<pointer<RelinkReferrer>> referrers;
};

struct DummyLiteral final : grammar::Literal {
  [[nodiscard]] bool isNullable() const noexcept override { return false; }
  [[nodiscard]] std::string_view getValue() const noexcept override {
    return "x";
  }
  [[nodiscard]] bool isCaseSensitive() const noexcept override { return true; }
};

const grammar::Literal &dummy_literal() noexcept {
  static const DummyLiteral literal;
  return literal;
}

template <typename OwnerType, typename TargetType>
struct TestReferenceAssignment final : grammar::Assignment {
  explicit TestReferenceAssignment(std::string_view feature) noexcept
      : feature(feature) {}

  [[nodiscard]] constexpr bool isNullable() const noexcept override {
    return false;
  }
  void execute(AstNode *, const CstNodeView &,
               const parser::ValueBuildContext &) const override {}
  [[nodiscard]] grammar::FeatureValue
  getValue(const AstNode *) const override {
    return {};
  }
  [[nodiscard]] const grammar::AbstractElement *getElement() const noexcept override {
    return nullptr;
  }
  [[nodiscard]] std::string_view getFeature() const noexcept override {
    return feature;
  }
  [[nodiscard]] bool isReference() const noexcept override { return true; }
  [[nodiscard]] bool isMultiReference() const noexcept override {
    return false;
  }
  [[nodiscard]] std::type_index getType() const noexcept override {
    return std::type_index(typeid(TargetType));
  }

  std::string_view feature;
};

class EmptyScopeProvider final : public references::ScopeProvider {
public:
  const workspace::AstNodeDescription *
  getScopeEntry(const ReferenceInfo &) const override {
    return nullptr;
  }

  bool visitScopeEntries(
      const ReferenceInfo &,
      utils::function_ref<bool(const workspace::AstNodeDescription &)>) const override {
    return true;
  }
};

// Resolves a reference to a single preconfigured (possibly cross-document)
// target description.
class SingleEntryScopeProvider final : public references::ScopeProvider {
public:
  std::optional<workspace::AstNodeDescription> entry;

  const workspace::AstNodeDescription *
  getScopeEntry(const ReferenceInfo &context) const override {
    if (entry.has_value() && entry->name == context.referenceText) {
      return std::addressof(*entry);
    }
    return nullptr;
  }

  bool visitScopeEntries(
      const ReferenceInfo &context,
      utils::function_ref<bool(const workspace::AstNodeDescription &)> visitor)
      const override {
    if (entry.has_value() &&
        (context.referenceText.empty() || entry->name == context.referenceText)) {
      return visitor(*entry);
    }
    return true;
  }
};

class CountingLinker final : public references::DefaultLinker {
public:
  using references::DefaultLinker::DefaultLinker;

  void link(Document &document,
            const utils::CancellationToken &cancelToken) const override {
    ++linkCalls;
    references::DefaultLinker::link(document, cancelToken);
  }

  mutable std::size_t linkCalls = 0;
};

std::unique_ptr<const parser::Parser>
make_unresolved_reference_parser(const references::Linker *&linkerRef) {
  auto parser = std::make_unique<test::FakeParser>();
  parser->callback =
      [&linkerRef](parser::ParseResult &result, std::string_view text) {
    static const TestReferenceAssignment<RelinkReferrer, RelinkNode> assignment(
        "node");
    static const grammar::Literal &literal = dummy_literal();
    assert(linkerRef != nullptr);

    auto cst = std::make_unique<RootCstNode>(text::TextSnapshot::copy(text));
    result.astArena = std::make_unique<pegium::AstArena>(*cst);
    auto &arena = *result.astArena;
    auto *root = arena.create<RelinkRoot>();
    CstBuilder builder(*cst);
    builder.leaf(0, static_cast<TextOffset>(text.size()), &literal);
    root->setCstNode(cst->get(0));
    auto *referrer = arena.create<RelinkReferrer>();
    referrer->setCstNode(cst->get(0));
    referrer->node.initialize(*referrer, "missing", cst->get(0), assignment,
                              *linkerRef);
    root->referrers.push_back(referrer);
    referrer->setContainer(*root);

    result.value = root;
    result.cst = std::move(cst);
    result.references.push_back(ReferenceHandle::direct(&referrer->node));
  };
  return parser;
}

const pegium::JsonValue::Array &
default_code_actions(const pegium::Diagnostic &diagnostic) {
  EXPECT_TRUE(diagnostic.data.has_value());
  EXPECT_TRUE(diagnostic.data->isObject());
  const auto &data = diagnostic.data->object();
  const auto it = data.find(std::string(kDefaultCodeActionsKey));
  EXPECT_NE(it, data.end());
  EXPECT_TRUE(it->second.isArray());
  return it->second.array();
}

TEST(DefaultDocumentBuilderTest, UpdateBuildsChangedDocumentToValidatedState) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test");
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("builder.test"), "test", "content");

  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->state, DocumentState::Validated);
  EXPECT_TRUE(document->diagnostics.empty());
  EXPECT_TRUE(document->parseSucceeded());
}

TEST(DefaultDocumentBuilderTest,
     UpdateRelinksDocumentsWithExistingLinkingErrors) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  const references::Linker *linkerRef = nullptr;
  auto services = test::make_uninstalled_core_services(
      *shared, "relink", {".relink"}, {}, make_unresolved_reference_parser(linkerRef));
  pegium::installDefaultCoreServices(*services);
  services->references.scopeProvider = std::make_unique<EmptyScopeProvider>();
  auto linker = std::make_unique<CountingLinker>(*services);
  auto *linkerPtr = linker.get();
  services->references.linker = std::move(linker);
  linkerRef = linkerPtr;
  shared->serviceRegistry->registerServices(std::move(services));

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("relink.relink"), "relink", "content");

  ASSERT_NE(document, nullptr);
  ASSERT_EQ(document->state, DocumentState::Validated);
  ASSERT_EQ(linkerPtr->linkCalls, 1u);
  ASSERT_FALSE(document->parseResult.references.empty());
  ASSERT_TRUE(document->parseResult.references.front().getConst()->hasError());

  shared->workspace.documentBuilder->update({}, {});

  EXPECT_EQ(linkerPtr->linkCalls, 2u);
  EXPECT_EQ(document->state, DocumentState::Validated);
}

TEST(DefaultDocumentBuilderTest,
     UpdateClearsLazyCrossDocumentLinkWhenTargetDocumentIsDeleted) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  const references::Linker *linkerRef = nullptr;
  auto services = test::make_uninstalled_core_services(
      *shared, "relink", {".relink"}, {},
      make_unresolved_reference_parser(linkerRef));
  pegium::installDefaultCoreServices(*services);
  auto scopeProvider = std::make_unique<SingleEntryScopeProvider>();
  auto *scopeProviderPtr = scopeProvider.get();
  services->references.scopeProvider = std::move(scopeProvider);
  auto linker = std::make_unique<CountingLinker>(*services);
  linkerRef = linker.get();
  services->references.linker = std::move(linker);
  shared->serviceRegistry->registerServices(std::move(services));

  // Target document B holds the node the reference in A resolves to.
  auto documentB = std::make_shared<Document>(test::make_text_document(
      test::make_file_uri("lazy-target.relink"), "relink", "target"));
  shared->workspace.documents->addDocument(documentB);
  auto cstB = std::make_unique<RootCstNode>(
      text::TextSnapshot::copy(documentB->textDocument().getText()));
  cstB->attachDocument(*documentB);
  documentB->parseResult.astArena = std::make_unique<pegium::AstArena>(*cstB);
  documentB->parseResult.astArena->attachDocument(*documentB);
  CstBuilder builderB(*cstB);
  builderB.leaf(0, 6, &dummy_literal());
  auto *targetNode = documentB->parseResult.astArena->create<RelinkNode>();
  targetNode->name = "target";
  targetNode->setCstNode(cstB->get(0));
  documentB->parseResult.value = targetNode;
  documentB->parseResult.cst = std::move(cstB);
  documentB->state = DocumentState::Validated;

  // The fake parser emits a reference whose text is "missing"; resolve it to B.
  scopeProviderPtr->entry = workspace::AstNodeDescription{
      .name = "missing",
      .type = std::type_index(typeid(RelinkNode)),
      .documentId = documentB->id,
      .symbolId = documentB->makeSymbolId(*targetNode)};

  // Referring document A, built lazily so its reference is never indexed but is
  // resolved on demand, caching a raw pointer into B's arena.
  auto documentA = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("lazy-referrer.relink"));
  ASSERT_NE(documentA, nullptr);
  shared->workspace.documents->addDocument(documentA);
  BuildOptions lazyOptions;
  lazyOptions.eagerLinking = false;
  lazyOptions.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{documentA}, lazyOptions);
  ASSERT_EQ(documentA->state, DocumentState::Validated);

  auto *rootBefore =
      pegium::ast_ptr_cast<RelinkRoot>(documentA->parseResult.value);
  ASSERT_NE(rootBefore, nullptr);
  ASSERT_FALSE(rootBefore->referrers.empty());
  auto &reference = rootBefore->referrers.front()->node;
  ASSERT_EQ(reference.get(), targetNode);
  ASSERT_TRUE(reference.isResolved());

  // Delete B. A's cached cross-document resolution now points into freed memory;
  // the incremental update must drop it so a later access re-resolves instead of
  // dereferencing the dangling pointer.
  shared->workspace.documentBuilder->update(
      {}, std::array<DocumentId, 1>{documentB->id});

  auto *rootAfter =
      pegium::ast_ptr_cast<RelinkRoot>(documentA->parseResult.value);
  ASSERT_NE(rootAfter, nullptr);
  ASSERT_FALSE(rootAfter->referrers.empty());
  EXPECT_FALSE(rootAfter->referrers.front()->node.isResolved());
}

TEST(DefaultDocumentBuilderTest,
     ValidationDiagnosticsArePublishedOnValidatedPhase) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  pegium::Diagnostic diagnostic;
  diagnostic.message = "validation";
  diagnostic.begin = 1;
  diagnostic.end = 3;
  validator->diagnostics.push_back(diagnostic);
  services->validation.documentValidator = std::move(validator);

  shared->serviceRegistry->registerServices(std::move(services));

  std::vector<std::string> validatedUris;
  auto disposable = shared->workspace.documentBuilder->onDocumentPhase(
      DocumentState::Validated,
      [&validatedUris](const std::shared_ptr<Document> &document,
                       utils::CancellationToken) {
        validatedUris.push_back(document->uri);
      });

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("validated.test"), "test", "content");

  ASSERT_NE(document, nullptr);
  ASSERT_EQ(document->state, DocumentState::Validated);
  ASSERT_EQ(document->diagnostics.size(), 1u);
  EXPECT_EQ(document->diagnostics.front().message, "validation");
  EXPECT_EQ(validatedUris, std::vector<std::string>{document->uri});
}

// Table-driven cover for the single-ParseDiagnostic builder family: each row
// drives a FakeParser that emits exactly one parse diagnostic with a given
// shape, then asserts the resulting Document diagnostic message / span / code
// and the default code-action data (if any). Folded from twelve near-identical
// TEST cases that differed only in the diagnostic shape and the expected
// rendering; see the per-row `name`/SCOPED_TRACE for the original intent.
//
// The shared boilerplate (install services, register a FakeParser, open +
// build a document) lives in run_single_parse_diagnostic_case below; tests that
// emit more than one diagnostic, use a real RuleParser, or assert structurally
// different things (parseResult.value, etc.) are kept as their own TEST cases.

// Which grammar element a row attaches to a diagnostic / expectation. The
// concrete grammar objects are constructed once per case in the loop body
// because TerminalRule / DataTypeRule / keyword instances are not constexpr.
enum class SingleParseElement { None, IdRule, ModuleKeyword, ExpressionRule };

// What a row asserts about the diagnostic's default code-action data:
//   NoData     -> data must be absent.
//   Action     -> exactly one action with the given title / newText (and,
//                 when actionBegin/actionEnd are set, the action span).
//   Unchecked  -> the row does not look at data at all.
enum class SingleParseData { Unchecked, NoData, Action };

struct SingleParseDiagnosticCase {
  const char *name;
  const char *uri;
  const char *text;
  bool fullMatch;

  // Single emitted parse diagnostic.
  parser::ParseDiagnosticKind kind;
  TextOffset offset;
  TextOffset beginOffset;
  TextOffset endOffset;
  SingleParseElement diagnosticElement;
  const char *diagnosticMessage; // nullptr -> empty message

  // Parser expectation frontier (independent of diagnosticElement).
  SingleParseElement expectation;

  // Document-diagnostic assertions (nullopt -> not checked).
  const char *expectedMessage;     // nullptr -> message not checked
  const char *expectedCode;        // nullptr -> code not checked
  std::optional<std::uint32_t> expectedBegin;
  std::optional<std::uint32_t> expectedEnd;
  bool requireExactlyOneDiagnostic; // ASSERT_EQ(size, 1) vs ASSERT_FALSE(empty)

  // Code-action assertions.
  SingleParseData data;
  const char *actionTitle;
  const char *actionNewText;
  std::optional<std::int64_t> actionBegin;
  std::optional<std::int64_t> actionEnd;
};

// Result of building one row. Holds the SharedCoreServices alive alongside the
// Document because the Document holds non-owning references into those services;
// both must outlive the row's assertions.
struct SingleParseDiagnosticResult {
  std::unique_ptr<pegium::SharedCoreServices> services;
  std::shared_ptr<Document> document;
};

SingleParseDiagnosticResult
run_single_parse_diagnostic_case(const SingleParseDiagnosticCase &c,
                                 const grammar::AbstractRule &idRule,
                                 const grammar::AbstractRule &expressionRule,
                                 const grammar::Literal &moduleKeyword) {
  using namespace pegium::parser;

  const auto resolveElement =
      [&](SingleParseElement which) -> const grammar::AbstractElement * {
    switch (which) {
    case SingleParseElement::IdRule:
      return std::addressof(idRule);
    case SingleParseElement::ModuleKeyword:
      return std::addressof(moduleKeyword);
    case SingleParseElement::ExpressionRule:
      return std::addressof(expressionRule);
    case SingleParseElement::None:
      break;
    }
    return nullptr;
  };

  auto parser = std::make_unique<test::FakeParser>();
  parser->fullMatch = c.fullMatch;
  parser->parseDiagnostics.push_back(
      {.kind = c.kind,
       .offset = c.offset,
       .beginOffset = c.beginOffset,
       .endOffset = c.endOffset,
       .element = resolveElement(c.diagnosticElement),
       .message = c.diagnosticMessage ? c.diagnosticMessage : ""});

  switch (c.expectation) {
  case SingleParseElement::IdRule:
    parser->expectations = expectation_result({rule_expectation(idRule)});
    break;
  case SingleParseElement::ExpressionRule:
    parser->expectations =
        expectation_result({rule_expectation(expressionRule)});
    break;
  case SingleParseElement::ModuleKeyword:
    parser->expectations =
        expectation_result({keyword_expectation(moduleKeyword)});
    break;
  case SingleParseElement::None:
    break;
  }

  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = test::make_uninstalled_core_services(
        *shared, "test", {".test"}, {}, std::move(parser));
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri(c.uri), "test", c.text);
  return {.services = std::move(shared), .document = std::move(document)};
}

TEST(DefaultDocumentBuilderTest, ParseDiagnosticRenderingTable) {
  using namespace pegium::parser;
  using E = SingleParseElement;
  using D = SingleParseData;
  using K = ParseDiagnosticKind;

  static const SingleParseDiagnosticCase kCases[] = {
      // ParseInsertedDiagnosticUsesTokenTypeNameForTokenTypes
      {.name = "InsertedTokenTypeUsesTokenTypeName",
       .uri = "builder-parse-inserted.test",
       .text = "",
       .fullMatch = false,
       .kind = K::Inserted,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::IdRule,
       .diagnosticMessage = nullptr,
       .expectation = E::IdRule,
       .expectedMessage = "Expecting ID",
       .expectedCode = "parse.inserted",
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = false,
       .data = D::NoData},
      // ParseKeywordDiagnosticUsesLiteralValueForKeywords
      {.name = "KeywordInsertedUsesLiteralValue",
       .uri = "builder-parse-keyword.test",
       .text = "foo",
       .fullMatch = false,
       .kind = K::Inserted,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::ModuleKeyword,
       .diagnosticMessage = nullptr,
       .expectation = E::ModuleKeyword,
       .expectedMessage = "Expecting 'module' but found `foo`.",
       .expectedCode = nullptr,
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = false,
       .data = D::Action,
       .actionTitle = "Insert 'module'",
       .actionNewText = "module",
       .actionBegin = std::nullopt,
       .actionEnd = std::nullopt},
      // ParseIncompleteDiagnosticUsesExpectedTokenAtEndOfInput
      {.name = "IncompleteAtEndOfInputUsesExpectedToken",
       .uri = "builder-parse-incomplete-eof.test",
       .text = "",
       .fullMatch = false,
       .kind = K::Incomplete,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::IdRule,
       .diagnosticMessage = nullptr,
       .expectation = E::IdRule,
       .expectedMessage = "Expecting ID",
       .expectedCode = nullptr,
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = false,
       .data = D::NoData},
      // ParseDeletedDiagnosticAddsDefaultCodeActionData
      {.name = "DeletedAddsDeleteUnexpectedTextAction",
       .uri = "builder-parse-deleted-action.test",
       .text = "unexpected",
       .fullMatch = true,
       .kind = K::Deleted,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::None,
       .diagnosticMessage = nullptr,
       .expectation = E::None,
       .expectedMessage = nullptr,
       .expectedCode = nullptr,
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = true,
       .data = D::Action,
       .actionTitle = "Delete unexpected text",
       .actionNewText = "",
       .actionBegin = 0,
       .actionEnd = 10},
      // ParseReplacedLiteralDiagnosticAddsDefaultCodeActionData
      {.name = "ReplacedAddsReplaceWithLiteralAction",
       .uri = "builder-parse-replaced-action.test",
       .text = "modulx",
       .fullMatch = true,
       .kind = K::Replaced,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::ModuleKeyword,
       .diagnosticMessage = nullptr,
       .expectation = E::None,
       .expectedMessage = nullptr,
       .expectedCode = nullptr,
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = true,
       .data = D::Action,
       .actionTitle = "Replace with 'module'",
       .actionNewText = "module",
       .actionBegin = 0,
       .actionEnd = 6},
      // ParseReplacedLiteralDiagnosticUsesReportedSourceRangeAndText
      {.name = "ReplacedUsesReportedSourceRangeAndText",
       .uri = "builder-parse-replaced-range.test",
       .text = "modle name",
       .fullMatch = true,
       .kind = K::Replaced,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 5,
       .diagnosticElement = E::ModuleKeyword,
       .diagnosticMessage = nullptr,
       .expectation = E::None,
       .expectedMessage = "Expecting 'module' but found `modle`.",
       .expectedCode = nullptr,
       .expectedBegin = 0u,
       .expectedEnd = 5u,
       .requireExactlyOneDiagnostic = true,
       .data = D::Action,
       .actionTitle = "Replace with 'module'",
       .actionNewText = "module",
       .actionBegin = 0,
       .actionEnd = 5},
      // ParseInsertedRuleDiagnosticDoesNotAddDefaultCodeActionData
      {.name = "InsertedRuleAddsNoCodeActionData",
       .uri = "builder-parse-inserted-rule.test",
       .text = "",
       .fullMatch = false,
       .kind = K::Inserted,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::IdRule,
       .diagnosticMessage = nullptr,
       .expectation = E::IdRule,
       .expectedMessage = nullptr,
       .expectedCode = nullptr,
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = true,
       .data = D::NoData},
      // ParseRecoveredDiagnosticDoesNotAddDefaultCodeActionData
      {.name = "RecoveredAddsNoCodeActionData",
       .uri = "builder-parse-recovered.test",
       .text = "alpha",
       .fullMatch = true,
       .kind = K::Recovered,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::None,
       .diagnosticMessage = nullptr,
       .expectation = E::None,
       .expectedMessage = nullptr,
       .expectedCode = nullptr,
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = true,
       .data = D::NoData},
      // ParseConversionDiagnosticUsesDedicatedCodeAndSpan
      {.name = "ConversionUsesDedicatedCodeAndSpan",
       .uri = "builder-parse-conversion.test",
       .text = "name123",
       .fullMatch = true,
       .kind = K::ConversionError,
       .offset = 4,
       .beginOffset = 4,
       .endOffset = 7,
       .diagnosticElement = E::None,
       .diagnosticMessage = "bad value",
       .expectation = E::None,
       .expectedMessage = "bad value",
       .expectedCode = "parse.conversion",
       .expectedBegin = 4u,
       .expectedEnd = 7u,
       .requireExactlyOneDiagnostic = true,
       .data = D::Unchecked},
      // ParseIncompleteDiagnosticWithTrailingInputUsesDirectFrontier
      {.name = "IncompleteWithTrailingInputUsesDirectFrontier",
       .uri = "builder-parse-incomplete-trailing.test",
       .text = "module name\n2   *",
       .fullMatch = false,
       .kind = K::Incomplete,
       .offset = 12,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::None,
       .diagnosticMessage = nullptr,
       .expectation = E::ExpressionRule,
       .expectedMessage = "Expecting Expression but found `2   *`.",
       .expectedCode = nullptr,
       .expectedBegin = 12u,
       .expectedEnd = 12u,
       .requireExactlyOneDiagnostic = false,
       .data = D::Unchecked},
      // ParseIncompleteDiagnosticWithTrailingRecoveredSuffixUsesReportedRange
      {.name = "IncompleteWithRecoveredTailUsesReportedRange",
       .uri = "builder-parse-incomplete-recovered-tail.test",
       .text = "abcd   tail",
       .fullMatch = false,
       .kind = K::Incomplete,
       .offset = 7,
       .beginOffset = 4,
       .endOffset = 11,
       .diagnosticElement = E::None,
       .diagnosticMessage = "Unexpected input.",
       .expectation = E::None,
       .expectedMessage = "Unexpected input.",
       .expectedCode = nullptr,
       .expectedBegin = 4u,
       .expectedEnd = 11u,
       .requireExactlyOneDiagnostic = false,
       .data = D::Unchecked},
      // EmptyDocumentUsesDirectFrontierInsteadOfEmptyFoundToken
      {.name = "EmptyDocumentUsesDirectFrontier",
       .uri = "builder-empty-grammar-sequence.test",
       .text = "",
       .fullMatch = false,
       .kind = K::Inserted,
       .offset = 0,
       .beginOffset = 0,
       .endOffset = 0,
       .diagnosticElement = E::ModuleKeyword,
       .diagnosticMessage = nullptr,
       .expectation = E::ModuleKeyword,
       .expectedMessage = "Expecting 'module'",
       .expectedCode = nullptr,
       .expectedBegin = std::nullopt,
       .expectedEnd = std::nullopt,
       .requireExactlyOneDiagnostic = false,
       .data = D::Unchecked},
  };

  // Constructed once and shared across rows; selected per row via the
  // SingleParseElement enum (these grammar objects cannot be constexpr).
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  DataTypeRule<int> expression{"Expression", some(d)};
  static constexpr auto moduleKeyword = "module"_kw;

  for (const auto &c : kCases) {
    SCOPED_TRACE(c.name);

    auto result =
        run_single_parse_diagnostic_case(c, id, expression, moduleKeyword);
    const auto &document = result.document;

    ASSERT_NE(document, nullptr);
    if (c.requireExactlyOneDiagnostic) {
      ASSERT_EQ(document->diagnostics.size(), 1u);
    } else {
      ASSERT_FALSE(document->diagnostics.empty());
    }

    const auto &diagnostic = document->diagnostics.front();
    if (c.expectedMessage != nullptr) {
      EXPECT_EQ(diagnostic.message, c.expectedMessage);
    }
    if (c.expectedBegin.has_value()) {
      EXPECT_EQ(diagnostic.begin, *c.expectedBegin);
    }
    if (c.expectedEnd.has_value()) {
      EXPECT_EQ(diagnostic.end, *c.expectedEnd);
    }
    if (c.expectedCode != nullptr) {
      ASSERT_TRUE(diagnostic.code.has_value());
      EXPECT_EQ(std::get<std::string>(*diagnostic.code), c.expectedCode);
    }

    switch (c.data) {
    case D::NoData:
      EXPECT_FALSE(diagnostic.data.has_value());
      break;
    case D::Action: {
      const auto &actions = default_code_actions(diagnostic);
      ASSERT_EQ(actions.size(), 1u);
      const auto &action = actions.front().object();
      EXPECT_EQ(action.at("title").string(), c.actionTitle);
      EXPECT_EQ(action.at("newText").string(), c.actionNewText);
      if (c.actionBegin.has_value()) {
        EXPECT_EQ(action.at("begin").integer(), *c.actionBegin);
      }
      if (c.actionEnd.has_value()) {
        EXPECT_EQ(action.at("end").integer(), *c.actionEnd);
      }
      break;
    }
    case D::Unchecked:
      break;
    }
  }
}

TEST(DefaultDocumentBuilderTest,
     ParseInsertedDiagnosticAfterTriviaAnchorsAfterPreviousVisibleToken) {
  using namespace pegium::parser;

  struct EntryNode : pegium::AstNode {
    string moduleName;
    string definitionName;
  };

  const auto whitespace = some(s);
  TerminalRule<std::string> moduleId{"MODULE_ID", "A-Z"_cr + many(w)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<EntryNode> entry{
      "Entry", "module"_kw + assign<&EntryNode::moduleName>(moduleId) +
                   "def"_kw + assign<&EntryNode::definitionName>(id)};
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = test::make_uninstalled_core_services(
        *shared, "test", {".test"}, {},
        std::make_unique<test::RuleParser>(entry, skipper));
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("builder-parse-inserted-gap.test"), "test",
      "module \n\n def a");

  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->diagnostics.empty());
  EXPECT_EQ(document->diagnostics.front().message,
            "Expecting MODULE_ID but found `def a`.");
  EXPECT_EQ(document->diagnostics.front().begin, 10u);
  EXPECT_EQ(document->diagnostics.front().end, 10u);
  ASSERT_EQ(document->parseResult.value, nullptr);
}

TEST(DefaultDocumentBuilderTest,
     ConsecutiveInsertedDiagnosticsAtEndOfInputUseDirectFrontier) {
  using namespace pegium::parser;

  auto parser = std::make_unique<test::FakeParser>();
  parser->fullMatch = false;
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  static constexpr auto colon = ":"_kw;
  static constexpr auto semicolon = ";"_kw;
  const auto offset =
      static_cast<TextOffset>(std::string_view{"module aa\ndef "}.size());

  parser->parseDiagnostics.push_back({.kind = ParseDiagnosticKind::Inserted,
                                      .offset = offset,
                                      .element = std::addressof(id)});
  parser->parseDiagnostics.push_back({.kind = ParseDiagnosticKind::Inserted,
                                      .offset = offset,
                                      .element = std::addressof(colon)});
  parser->parseDiagnostics.push_back({.kind = ParseDiagnosticKind::Inserted,
                                      .offset = offset,
                                      .element = std::addressof(number)});
  parser->parseDiagnostics.push_back({.kind = ParseDiagnosticKind::Inserted,
                                      .offset = offset,
                                      .element = std::addressof(semicolon)});
  parser->expectations = expectation_result({rule_expectation(id)});

  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = test::make_uninstalled_core_services(
        *shared, "test", {".test"}, {}, std::move(parser));
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("builder-parse-inserted-eof-sequence.test"),
      "test", "module aa\ndef ");

  ASSERT_NE(document, nullptr);
  ASSERT_EQ(document->diagnostics.size(), 1u);
  EXPECT_EQ(document->diagnostics.front().message, "Expecting ID");
  ASSERT_TRUE(document->diagnostics.front().code.has_value());
  EXPECT_EQ(std::get<std::string>(*document->diagnostics.front().code),
            "parse.inserted");
}

TEST(DefaultDocumentBuilderTest,
     UpdateUsesBuiltInAndFastValidationCategoriesByDefault) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test");
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  auto *validatorPtr = validator.get();
  services->validation.documentValidator = std::move(validator);

  shared->serviceRegistry->registerServices(std::move(services));

  auto document = test::open_and_build_document(
      *shared, test::make_file_uri("default-update-validation.test"), "test",
      "content");

  ASSERT_NE(document, nullptr);
  ASSERT_EQ(validatorPtr->validateCalls, 1u);
  ASSERT_EQ(validatorPtr->seenOptions.size(), 1u);
  EXPECT_EQ(validatorPtr->seenOptions.front().categories,
            (std::vector<std::string>{"built-in", "fast"}));
}

TEST(DefaultDocumentBuilderTest, UpdateEmitsChangedAndDeletedDocumentIds) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto existing = std::make_shared<Document>(
      test::make_text_document(test::make_file_uri("deleted.test"), "test",
                               "obsolete"));
  shared->workspace.documents->addDocument(existing);

  std::vector<DocumentId> changed;
  std::vector<DocumentId> deleted;
  auto disposable = shared->workspace.documentBuilder->onUpdate(
      [&changed, &deleted](std::span<const DocumentId> changedDocumentIds,
                           std::span<const DocumentId> deletedDocumentIds) {
        changed.assign(changedDocumentIds.begin(), changedDocumentIds.end());
        deleted.assign(deletedDocumentIds.begin(), deletedDocumentIds.end());
      });

  const auto changedUri = test::make_file_uri("changed.test");
  auto textDocuments = test::text_documents(*shared);
  ASSERT_NE(textDocuments, nullptr);
  ASSERT_NE(
      test::set_text_document(*textDocuments, changedUri, "test", "content", 1),
      nullptr);

  const auto changedDocumentId =
      shared->workspace.documents->getOrCreateDocumentId(changedUri);
  const std::array<DocumentId, 1> changedDocumentIds{changedDocumentId};
  const std::array<DocumentId, 1> deletedDocumentIds{existing->id};
  (void)shared->workspace.documentBuilder->update(changedDocumentIds,
                                                  deletedDocumentIds);

  EXPECT_EQ(changed, std::vector<DocumentId>{changedDocumentId});
  EXPECT_EQ(deleted, std::vector<DocumentId>{existing->id});
}

TEST(DefaultDocumentBuilderTest, DisposablesCanOutliveBuilder) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);

  utils::ScopedDisposable updateSubscription;
  utils::ScopedDisposable buildSubscription;
  utils::ScopedDisposable documentSubscription;
  {
    auto builder = std::make_unique<DefaultDocumentBuilder>(*shared);
    updateSubscription = builder->onUpdate(
        [](std::span<const DocumentId>, std::span<const DocumentId>) {});
    buildSubscription = builder->onBuildPhase(
        DocumentState::Validated,
        [](std::span<const std::shared_ptr<Document>>,
           utils::CancellationToken) {});
    documentSubscription = builder->onDocumentPhase(
        DocumentState::Validated,
        [](const std::shared_ptr<Document> &, utils::CancellationToken) {});
  }

  EXPECT_FALSE(updateSubscription.disposed());
  EXPECT_FALSE(buildSubscription.disposed());
  EXPECT_FALSE(documentSubscription.disposed());
}

TEST(DefaultDocumentBuilderTest,
     UpdateMaterializesChangedDocumentFromOpenTextDocument) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  const auto uri = test::make_file_uri("materialized.test");
  auto textDocuments = test::text_documents(*shared);
  ASSERT_NE(textDocuments, nullptr);
  auto textDocument =
      test::set_text_document(*textDocuments, uri, "test", "content", 1);
  ASSERT_NE(textDocument, nullptr);

  const auto documentId =
      shared->workspace.documents->getOrCreateDocumentId(textDocument->uri());
  const std::array<DocumentId, 1> changedDocumentIds{documentId};
  (void)shared->workspace.documentBuilder->update(changedDocumentIds, {});

  auto document = shared->workspace.documents->getDocument(documentId);
  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->uri, uri);
  EXPECT_EQ(document->textDocument().languageId(), "test");
  EXPECT_EQ(document->textDocument().getText(), "content");
  EXPECT_EQ(document->state, DocumentState::Validated);
}

TEST(DefaultDocumentBuilderTest, UpdatePrioritizesOpenTextDocuments) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  const auto closedUri = test::make_file_uri("closed-priority.test");
  auto closedDocument =
      shared->workspace.documentFactory->fromString("closed", closedUri);
  ASSERT_NE(closedDocument, nullptr);
  shared->workspace.documents->addDocument(closedDocument);

  const auto openUri = test::make_file_uri("open-priority.test");
  auto openDocument =
      shared->workspace.documentFactory->fromString("open", openUri);
  ASSERT_NE(openDocument, nullptr);
  shared->workspace.documents->addDocument(openDocument);
  auto textDocuments = test::text_documents(*shared);
  ASSERT_NE(textDocuments, nullptr);
  ASSERT_NE(test::set_text_document(*textDocuments, openUri, "test", "open", 2),
            nullptr);

  std::vector<std::string> validatedUris;
  auto disposable = shared->workspace.documentBuilder->onDocumentPhase(
      DocumentState::Validated,
      [&validatedUris](const std::shared_ptr<Document> &document,
                       utils::CancellationToken) {
        validatedUris.push_back(document->uri);
      });

  (void)shared->workspace.documentBuilder->update({}, {});

  ASSERT_EQ(validatedUris.size(), 2u);
  EXPECT_EQ(validatedUris.front(), openUri);
  EXPECT_EQ(validatedUris.back(), closedUri);
}

TEST(DefaultDocumentBuilderTest, BuildDoesNotValidateByDefault) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test", {".test"});
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  auto *validatorPtr = validator.get();
  services->validation.documentValidator = std::move(validator);

  shared->serviceRegistry->registerServices(std::move(services));

  const auto uri = test::make_file_uri("build-no-validation.test");
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  bool validatedPhaseReached = false;
  auto validatedDisposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Validated,
      [&validatedPhaseReached](std::span<const std::shared_ptr<Document>>,
                               utils::CancellationToken) {
        validatedPhaseReached = true;
      });

  const std::array<std::shared_ptr<Document>, 1> documents{document};
  shared->workspace.documentBuilder->build(documents);

  EXPECT_EQ(validatorPtr->validateCalls, 0u);
  EXPECT_FALSE(validatedPhaseReached);
  EXPECT_EQ(document->state, DocumentState::IndexedReferences);
}

TEST(DefaultDocumentBuilderTest, BuildEmitsUpdateAndAllBuildPhasesInOrder) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  const auto uri = test::make_file_uri("phases.test");
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  std::vector<DocumentId> updatedDocumentIds;
  std::vector<DocumentState> phases;
  auto updateDisposable = shared->workspace.documentBuilder->onUpdate(
      [&updatedDocumentIds](std::span<const DocumentId> changedDocumentIds,
                            std::span<const DocumentId>) {
        updatedDocumentIds.assign(changedDocumentIds.begin(),
                                  changedDocumentIds.end());
      });
  auto parsedDisposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Parsed,
      [&phases](std::span<const std::shared_ptr<Document>>,
                utils::CancellationToken) {
        phases.push_back(DocumentState::Parsed);
      });
  auto indexedContentDisposable =
      shared->workspace.documentBuilder->onBuildPhase(
          DocumentState::IndexedContent,
          [&phases](std::span<const std::shared_ptr<Document>>,
                    utils::CancellationToken) {
            phases.push_back(DocumentState::IndexedContent);
          });
  auto computedScopesDisposable =
      shared->workspace.documentBuilder->onBuildPhase(
          DocumentState::ComputedScopes,
          [&phases](std::span<const std::shared_ptr<Document>>,
                    utils::CancellationToken) {
            phases.push_back(DocumentState::ComputedScopes);
          });
  auto linkedDisposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Linked,
      [&phases](std::span<const std::shared_ptr<Document>>,
                utils::CancellationToken) {
        phases.push_back(DocumentState::Linked);
      });
  auto indexedReferencesDisposable =
      shared->workspace.documentBuilder->onBuildPhase(
          DocumentState::IndexedReferences,
          [&phases](std::span<const std::shared_ptr<Document>>,
                    utils::CancellationToken) {
            phases.push_back(DocumentState::IndexedReferences);
          });
  auto validatedDisposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Validated,
      [&phases](std::span<const std::shared_ptr<Document>>,
                utils::CancellationToken) {
        phases.push_back(DocumentState::Validated);
      });

  BuildOptions options;
  options.validation = true;
  const std::array<std::shared_ptr<Document>, 1> documents{document};
  shared->workspace.documentBuilder->build(documents, options);
  EXPECT_EQ(updatedDocumentIds, std::vector<DocumentId>{document->id});
  EXPECT_EQ(phases,
            (std::vector<DocumentState>{
                DocumentState::Parsed, DocumentState::IndexedContent,
                DocumentState::ComputedScopes, DocumentState::Linked,
                DocumentState::IndexedReferences, DocumentState::Validated}));
  EXPECT_EQ(document->state, DocumentState::Validated);
}

TEST(DefaultDocumentBuilderTest,
     BuildSkipsLinkingPhasesWhenEagerLinkingIsDisabled) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  const auto uri = test::make_file_uri("phases-no-link.test");
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  std::vector<DocumentState> phases;
  auto parsedDisposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Parsed,
      [&phases](std::span<const std::shared_ptr<Document>>,
                utils::CancellationToken) {
        phases.push_back(DocumentState::Parsed);
      });
  auto indexedContentDisposable =
      shared->workspace.documentBuilder->onBuildPhase(
          DocumentState::IndexedContent,
          [&phases](std::span<const std::shared_ptr<Document>>,
                    utils::CancellationToken) {
            phases.push_back(DocumentState::IndexedContent);
          });
  auto computedScopesDisposable =
      shared->workspace.documentBuilder->onBuildPhase(
          DocumentState::ComputedScopes,
          [&phases](std::span<const std::shared_ptr<Document>>,
                    utils::CancellationToken) {
            phases.push_back(DocumentState::ComputedScopes);
          });
  auto linkedDisposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Linked,
      [&phases](std::span<const std::shared_ptr<Document>>,
                utils::CancellationToken) {
        phases.push_back(DocumentState::Linked);
      });
  auto indexedReferencesDisposable =
      shared->workspace.documentBuilder->onBuildPhase(
          DocumentState::IndexedReferences,
          [&phases](std::span<const std::shared_ptr<Document>>,
                    utils::CancellationToken) {
            phases.push_back(DocumentState::IndexedReferences);
          });
  auto validatedDisposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Validated,
      [&phases](std::span<const std::shared_ptr<Document>>,
                utils::CancellationToken) {
        phases.push_back(DocumentState::Validated);
      });

  BuildOptions options;
  options.eagerLinking = false;
  options.validation = true;
  const std::array<std::shared_ptr<Document>, 1> documents{document};
  shared->workspace.documentBuilder->build(documents, options);

  EXPECT_EQ(phases, (std::vector<DocumentState>{DocumentState::Parsed,
                                                DocumentState::IndexedContent,
                                                DocumentState::ComputedScopes,
                                                DocumentState::Validated}));
  EXPECT_EQ(document->state, DocumentState::Validated);
}

TEST(DefaultDocumentBuilderTest,
     BuildRevalidatesOnlyMissingValidationCategoriesAndAppendsDiagnostics) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test", {".test"});
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  auto *validatorPtr = validator.get();
  validator->diagnosticsByCall = {
      pegium::Diagnostic{.message = "fast-diagnostic"},
      pegium::Diagnostic{.message = "slow-diagnostic"},
  };
  services->validation.documentValidator = std::move(validator);

  auto registry =
      std::make_unique<validation::DefaultValidationRegistry>(*services);
  registry->registerCheck<AstNode>(
      [](const AstNode &, const validation::ValidationAcceptor &) {}, "fast");
  registry->registerCheck<AstNode>(
      [](const AstNode &, const validation::ValidationAcceptor &) {}, "slow");
  services->validation.validationRegistry = std::move(registry);

  shared->serviceRegistry->registerServices(std::move(services));

  const auto uri = test::make_file_uri("partial-validation.test");
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  BuildOptions fastOnly;
  fastOnly.validation = validation::ValidationOptions{.categories = {"fast"}};
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, fastOnly);

  ASSERT_EQ(validatorPtr->validateCalls, 1u);
  ASSERT_EQ(document->diagnostics.size(), 1u);
  EXPECT_EQ(document->diagnostics.front().message, "fast-diagnostic");

  BuildOptions fastAndSlow;
  fastAndSlow.validation =
      validation::ValidationOptions{.categories = {"fast", "slow"}};
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, fastAndSlow);

  ASSERT_EQ(validatorPtr->validateCalls, 2u);
  ASSERT_EQ(validatorPtr->seenOptions.size(), 2u);
  EXPECT_EQ(validatorPtr->seenOptions[0].categories,
            (std::vector<std::string>{"fast"}));
  EXPECT_EQ(validatorPtr->seenOptions[1].categories,
            (std::vector<std::string>{"slow"}));
  ASSERT_EQ(document->diagnostics.size(), 2u);
  EXPECT_EQ(document->diagnostics[0].message, "fast-diagnostic");
  EXPECT_EQ(document->diagnostics[1].message, "slow-diagnostic");

  // Regression: a third build with the same categories must not re-validate
  // anything. The cumulative validation-check history records that both "fast"
  // and "slow" already ran, so no category is missing. Resetting that history
  // (an unconditional state.result.emplace()) made this build re-run "fast"
  // and append a duplicate diagnostic.
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, fastAndSlow);

  EXPECT_EQ(validatorPtr->validateCalls, 2u);
  EXPECT_EQ(validatorPtr->seenOptions.size(), 2u);
  EXPECT_EQ(document->diagnostics.size(), 2u);
}

TEST(DefaultDocumentBuilderTest,
     BuildStripsStopAfterFlagsSoIncrementalValidationStaysComplete) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test", {".test"});
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  auto *validatorPtr = validator.get();
  services->validation.documentValidator = std::move(validator);

  auto registry =
      std::make_unique<validation::DefaultValidationRegistry>(*services);
  registry->registerCheck<AstNode>(
      [](const AstNode &, const validation::ValidationAcceptor &) {}, "fast");
  services->validation.validationRegistry = std::move(registry);

  shared->serviceRegistry->registerServices(std::move(services));

  const auto uri = test::make_file_uri("stop-after-strip.test");
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  BuildOptions options;
  options.validation =
      validation::ValidationOptions{.categories = {"fast"},
                                    .stopAfterParsingErrors = true,
                                    .stopAfterLinkingErrors = true};
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);

  // The incremental builder records the categories it validated, so it must
  // validate them fully: the stop-after flags are stripped before the validator
  // runs. Leaving them would let a skipped category be recorded as executed and
  // never re-validated on a later build.
  ASSERT_EQ(validatorPtr->validateCalls, 1u);
  ASSERT_EQ(validatorPtr->seenOptions.size(), 1u);
  EXPECT_EQ(validatorPtr->seenOptions[0].categories,
            (std::vector<std::string>{"fast"}));
  EXPECT_FALSE(validatorPtr->seenOptions[0].stopAfterParsingErrors.has_value());
  EXPECT_FALSE(validatorPtr->seenOptions[0].stopAfterLinkingErrors.has_value());
}

TEST(DefaultDocumentBuilderTest,
     ResetToStateClearsBuildStateBelowIndexedReferences) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test", {".test"});
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  auto *validatorPtr = validator.get();
  services->validation.documentValidator = std::move(validator);

  auto registry =
      std::make_unique<validation::DefaultValidationRegistry>(*services);
  registry->registerCheck<AstNode>(
      [](const AstNode &, const validation::ValidationAcceptor &) {}, "fast");
  services->validation.validationRegistry = std::move(registry);

  shared->serviceRegistry->registerServices(std::move(services));

  const auto uri = test::make_file_uri("reset-build-state.test");
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  BuildOptions options;
  options.validation = validation::ValidationOptions{.categories = {"fast"}};
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);
  ASSERT_EQ(validatorPtr->validateCalls, 1u);

  shared->workspace.documentBuilder->resetToState(
      *document, DocumentState::ComputedScopes);

  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);
  EXPECT_EQ(validatorPtr->validateCalls, 2u);
}

TEST(DefaultDocumentBuilderTest, ResetToStateChangedClearsDiagnostics) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test", {".test"});
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  validator->diagnostics.push_back(pegium::Diagnostic{.message = "validation"});
  services->validation.documentValidator = std::move(validator);

  shared->serviceRegistry->registerServices(std::move(services));

  const auto uri = test::make_file_uri("reset-changed.test");
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  BuildOptions options;
  options.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);
  ASSERT_EQ(document->state, DocumentState::Validated);
  ASSERT_FALSE(document->diagnostics.empty());

  shared->workspace.documentBuilder->resetToState(*document,
                                                  DocumentState::Changed);
  EXPECT_EQ(document->state, DocumentState::Changed);
  EXPECT_TRUE(document->diagnostics.empty());
}

TEST(DefaultDocumentBuilderTest,
     UpdateKeepsIncompleteBuildOptionsUntilPreviousBuildCompletes) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  auto services = test::make_uninstalled_core_services(*shared, "test", {".test"});
  pegium::installDefaultCoreServices(*services);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  auto *validatorPtr = validator.get();
  services->validation.documentValidator = std::move(validator);

  shared->serviceRegistry->registerServices(std::move(services));

  const auto uri = test::make_file_uri("resume-incomplete-build.test");
  auto fileSystem = std::make_shared<test::FakeFileSystemProvider>();
  fileSystem->files["/tmp/pegium-tests/resume-incomplete-build.test"] =
      "content";
  shared->workspace.fileSystemProvider = fileSystem;
  auto document =
      shared->workspace.documentFactory->fromString("content", uri);
  ASSERT_NE(document, nullptr);
  document->state = DocumentState::Changed;
  shared->workspace.documents->addDocument(document);

  auto &updateOptions = shared->workspace.documentBuilder->updateBuildOptions();
  updateOptions.eagerLinking = false;
  updateOptions.validation = false;

  utils::CancellationTokenSource cancellationSource;
  bool cancelled = false;
  auto disposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Parsed, [&cancellationSource, &cancelled](
                                 std::span<const std::shared_ptr<Document>>,
                                 utils::CancellationToken) {
        if (!cancelled) {
          cancelled = true;
          cancellationSource.request_stop();
        }
      });

  EXPECT_THROW((void)shared->workspace.documentBuilder->update(
                   {}, {}, cancellationSource.get_token()),
               utils::OperationCancelled);
  // Parsing and content indexing share one phase: cancelling from the Parsed
  // build-phase callback stops at that phase's boundary (IndexedContent).
  ASSERT_EQ(document->state, DocumentState::IndexedContent);

  updateOptions.eagerLinking = true;
  updateOptions.validation = true;

  ASSERT_NO_THROW((void)shared->workspace.documentBuilder->update({}, {}));
  EXPECT_EQ(document->state, DocumentState::ComputedScopes);
  EXPECT_EQ(validatorPtr->validateCalls, 0u);

  ASSERT_NO_THROW((void)shared->workspace.documentBuilder->update({}, {}));
  EXPECT_EQ(document->state, DocumentState::Validated);
  EXPECT_EQ(validatorPtr->validateCalls, 1u);
}

TEST(DefaultDocumentBuilderTest,
     WaitUntilThrowsWhenWorkspaceAlreadyValidatedExcludedDocument) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto excludedDocument = shared->workspace.documentFactory->fromString(
      "excluded", test::make_file_uri("wait-excluded.test"));
  auto validatedDocument = shared->workspace.documentFactory->fromString(
      "validated", test::make_file_uri("wait-validated.test"));
  ASSERT_NE(excludedDocument, nullptr);
  ASSERT_NE(validatedDocument, nullptr);
  shared->workspace.documents->addDocument(excludedDocument);
  shared->workspace.documents->addDocument(validatedDocument);

  BuildOptions parseOnlyOptions;
  parseOnlyOptions.validation = false;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{excludedDocument},
      parseOnlyOptions);
  ASSERT_EQ(excludedDocument->state, DocumentState::IndexedReferences);

  BuildOptions validatedOptions;
  validatedOptions.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{validatedDocument},
      validatedOptions);
  ASSERT_EQ(validatedDocument->state, DocumentState::Validated);

  try {
    (void)shared->workspace.documentBuilder->waitUntil(
        DocumentState::Validated, excludedDocument->id);
    FAIL() << "waitUntil should fail for a document excluded from validation.";
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("requiring Validated"), std::string::npos);
    EXPECT_NE(message.find("workspace state is already Validated"),
              std::string::npos);
  }
}

TEST(DefaultDocumentBuilderTest,
     WaitUntilDocumentUnblocksWhenWorkspaceAdvancesPastExcludedDocument) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices =
        test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("wait-excluded-armed-before.test"));
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);
  const auto documentId = document->id;

  // Arm the waiter before the build. The document is excluded from validation,
  // so it never reaches Validated; the waiter must still terminate (reporting
  // the lag) once the workspace advances past it, not block forever. A
  // cancellable token lets us release a still-blocked waiter so a regression
  // does not deadlock the future destructor.
  std::stop_source stop;
  auto waiter =
      std::async(std::launch::async, [&shared, documentId, &stop]() -> std::string {
        try {
          (void)shared->workspace.documentBuilder->waitUntil(
              DocumentState::Validated, documentId, stop.get_token());
          return "returned-without-error";
        } catch (const std::exception &error) {
          return error.what();
        }
      });
  EXPECT_EQ(waiter.wait_for(20ms), std::future_status::timeout);

  BuildOptions options;
  options.validation = false;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);
  ASSERT_EQ(document->state, DocumentState::IndexedReferences);

  const bool resolved = waiter.wait_for(2s) == std::future_status::ready;
  if (!resolved) {
    stop.request_stop();
  }
  ASSERT_TRUE(resolved)
      << "awaitDocumentState blocked instead of reporting the lagging document";
  const std::string message = waiter.get();
  EXPECT_NE(message.find("requiring Validated"), std::string::npos);
  EXPECT_NE(message.find("workspace state is already Validated"),
            std::string::npos);
}

TEST(DefaultDocumentBuilderTest, WaitUntilDocumentReturnsBuiltDocumentId) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("wait-success.test"));
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  auto waiter = std::async(std::launch::async, [&shared, document]() {
    return shared->workspace.documentBuilder->waitUntil(
        DocumentState::Validated, document->id);
  });

  BuildOptions options;
  options.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);

  EXPECT_EQ(waiter.get(), document->id);
}

TEST(DefaultDocumentBuilderTest,
     WaitUntilDocumentIsMemorySafeWithConcurrentWaitersAndBuild) {
  // Concurrency smoke test for the supported pattern: several threads call
  // waitUntil(documentId) while a build advances and notifies the awaited state.
  // It exercises awaitDocumentState's wait-state lifetime against the build
  // thread's phase-listener invocation, the site of the use-after-free fixed by
  // holding the wait state in a heap WaitState captured by value. (A
  // deterministic single-shot reproduction of that race is not possible without
  // an artificial pause between the listener snapshot and its invocation; the
  // guarantee rests on the by-value shared_ptr capture, which makes a late
  // listener invocation operate on still-alive state by construction.)
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices =
        test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  BuildOptions options;
  options.validation = true;
  auto &builder = *shared->workspace.documentBuilder;

  constexpr int kIterations = 200;
  constexpr int kWaitersPerBuild = 4;
  for (int i = 0; i < kIterations; ++i) {
    auto document = shared->workspace.documentFactory->fromString(
        "content",
        test::make_file_uri("wait-race-" + std::to_string(i) + ".test"));
    ASSERT_NE(document, nullptr);
    shared->workspace.documents->addDocument(document);

    std::vector<std::future<void>> waiters;
    waiters.reserve(kWaitersPerBuild);
    for (int w = 0; w < kWaitersPerBuild; ++w) {
      waiters.push_back(std::async(std::launch::async, [&builder, document]() {
        try {
          (void)builder.waitUntil(DocumentState::Validated, document->id);
        } catch (...) {
          // Only memory safety is under test here; a concurrent observation may
          // legitimately throw.
        }
      }));
    }

    builder.build(std::array<std::shared_ptr<Document>, 1>{document}, options);

    for (auto &waiter : waiters) {
      waiter.get();
    }
  }
}

TEST(DefaultDocumentBuilderTest, WaitUntilWorkspaceUnblocksWhenArmedBeforeBuild) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("wait-workspace-success.test"));
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  auto waiter = std::async(std::launch::async, [&shared]() {
    shared->workspace.documentBuilder->waitUntil(DocumentState::Validated);
  });
  EXPECT_EQ(waiter.wait_for(20ms), std::future_status::timeout);

  BuildOptions options;
  options.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);

  EXPECT_EQ(waiter.wait_for(1s), std::future_status::ready);
  EXPECT_NO_THROW(waiter.get());
}

TEST(DefaultDocumentBuilderTest,
     WaitUntilWorkspaceResolvesAfterBuildPhaseCallbacksForSameState) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices =
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("wait-workspace-order.test"));
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  std::mutex eventsMutex;
  std::vector<std::string> events;
  std::future<void> waiter;
  auto waiterStartedPromise = std::make_shared<std::promise<void>>();
  auto waiterStarted = waiterStartedPromise->get_future();
  auto disposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Validated,
      [&shared, &events, &eventsMutex, &waiter, &waiterStarted,
       waiterStartedPromise](
          std::span<const std::shared_ptr<Document>>, utils::CancellationToken) {
        {
          std::scoped_lock lock(eventsMutex);
          events.push_back("B");
        }
        waiter = std::async(std::launch::async,
                            [&shared, &events, &eventsMutex,
                             waiterStartedPromise]() {
                              waiterStartedPromise->set_value();
                              shared->workspace.documentBuilder->waitUntil(
                                  DocumentState::Validated);
                              std::scoped_lock lock(eventsMutex);
                              events.push_back("W");
                            });
        EXPECT_EQ(waiterStarted.wait_for(1s), std::future_status::ready);
        EXPECT_EQ(waiter.wait_for(20ms), std::future_status::timeout);
      });

  BuildOptions options;
  options.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);

  ASSERT_TRUE(waiter.valid());
  EXPECT_EQ(waiter.wait_for(1s), std::future_status::ready);
  EXPECT_NO_THROW(waiter.get());
  EXPECT_EQ(events, (std::vector<std::string>{"B", "W"}));
}

TEST(DefaultDocumentBuilderTest, WaitUntilWorkspaceUnblocksMultipleWaiters) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices =
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("wait-workspace-multiple.test"));
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  auto waiter1 = std::async(std::launch::async, [&shared]() {
    shared->workspace.documentBuilder->waitUntil(DocumentState::Validated);
  });
  auto waiter2 = std::async(std::launch::async, [&shared]() {
    shared->workspace.documentBuilder->waitUntil(DocumentState::Validated);
  });
  EXPECT_EQ(waiter1.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(waiter2.wait_for(20ms), std::future_status::timeout);

  BuildOptions options;
  options.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options);

  EXPECT_EQ(waiter1.wait_for(1s), std::future_status::ready);
  EXPECT_EQ(waiter2.wait_for(1s), std::future_status::ready);
  EXPECT_NO_THROW(waiter1.get());
  EXPECT_NO_THROW(waiter2.get());
}

TEST(DefaultDocumentBuilderTest,
     WaitUntilWorkspaceSurvivesCancelledBuildAndResolvesOnRetry) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices =
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("wait-workspace-retry.test"));
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  auto waiter = std::async(std::launch::async, [&shared]() {
    shared->workspace.documentBuilder->waitUntil(DocumentState::Validated);
  });

  utils::CancellationTokenSource cancellationSource;
  bool cancelled = false;
  auto disposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::ComputedScopes,
      [&cancellationSource, &cancelled](std::span<const std::shared_ptr<Document>>,
                                        utils::CancellationToken) {
        if (!cancelled) {
          cancelled = true;
          cancellationSource.request_stop();
        }
      });

  BuildOptions options;
  options.validation = true;
  EXPECT_THROW(shared->workspace.documentBuilder->build(
                   std::array<std::shared_ptr<Document>, 1>{document}, options,
                   cancellationSource.get_token()),
               utils::OperationCancelled);
  // ComputedScopes is computed inside the same phase that links and indexes
  // references, so cancelling from the ComputedScopes build-phase callback (which
  // runs once that phase has drained) stops the build at the phase boundary —
  // the document has reached IndexedReferences but not yet Validated.
  EXPECT_EQ(document->state, DocumentState::IndexedReferences);
  EXPECT_EQ(waiter.wait_for(20ms), std::future_status::timeout);

  EXPECT_NO_THROW(shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document}, options));
  EXPECT_EQ(waiter.wait_for(1s), std::future_status::ready);
  EXPECT_NO_THROW(waiter.get());
}

TEST(DefaultDocumentBuilderTest,
     WaitUntilWorkspaceResolvesWhenValidatedPhaseHasNoDocuments) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices =
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto document = shared->workspace.documentFactory->fromString(
      "content", test::make_file_uri("wait-workspace-empty-phase.test"));
  ASSERT_NE(document, nullptr);
  shared->workspace.documents->addDocument(document);

  bool validatedPhaseReached = false;
  auto disposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Validated,
      [&validatedPhaseReached](std::span<const std::shared_ptr<Document>>,
                               utils::CancellationToken) {
        validatedPhaseReached = true;
      });
  auto waiter = std::async(std::launch::async, [&shared]() {
    shared->workspace.documentBuilder->waitUntil(DocumentState::Validated);
  });
  EXPECT_EQ(waiter.wait_for(20ms), std::future_status::timeout);

  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{document});

  EXPECT_FALSE(validatedPhaseReached);
  EXPECT_EQ(document->state, DocumentState::IndexedReferences);
  EXPECT_EQ(waiter.wait_for(1s), std::future_status::ready);
  EXPECT_NO_THROW(waiter.get());
}

TEST(DefaultDocumentBuilderTest,
     WaitUntilThrowsWhenWorkspaceAlreadyValidatedInterruptedDocument) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  {
    auto registeredServices = 
      test::make_uninstalled_core_services(*shared, "test", {".test"});
    pegium::installDefaultCoreServices(*registeredServices);
    shared->serviceRegistry->registerServices(std::move(registeredServices));
  }

  auto interruptedDocument = shared->workspace.documentFactory->fromString(
      "interrupted", test::make_file_uri("wait-interrupted.test"));
  auto validatedDocument = shared->workspace.documentFactory->fromString(
      "validated", test::make_file_uri("wait-after-interrupted.test"));
  ASSERT_NE(interruptedDocument, nullptr);
  ASSERT_NE(validatedDocument, nullptr);
  shared->workspace.documents->addDocument(interruptedDocument);
  shared->workspace.documents->addDocument(validatedDocument);

  utils::CancellationTokenSource updateCancellation;
  bool cancelled = false;
  auto disposable = shared->workspace.documentBuilder->onBuildPhase(
      DocumentState::Parsed, [&updateCancellation, &cancelled](
                                 std::span<const std::shared_ptr<Document>>,
                                 utils::CancellationToken) {
        if (!cancelled) {
          cancelled = true;
          updateCancellation.request_stop();
        }
      });

  EXPECT_THROW((void)shared->workspace.documentBuilder->update(
                   {}, {}, updateCancellation.get_token()),
               utils::OperationCancelled);
  // Parsing and content indexing share one phase, so cancelling from the Parsed
  // build-phase callback stops the build at that phase's boundary: the document
  // has reached IndexedContent.
  ASSERT_EQ(interruptedDocument->state, DocumentState::IndexedContent);

  BuildOptions validatedOptions;
  validatedOptions.validation = true;
  shared->workspace.documentBuilder->build(
      std::array<std::shared_ptr<Document>, 1>{validatedDocument},
      validatedOptions);
  ASSERT_EQ(validatedDocument->state, DocumentState::Validated);

  try {
    (void)shared->workspace.documentBuilder->waitUntil(
        DocumentState::Validated, interruptedDocument->id);
    FAIL() << "waitUntil should fail once the workspace has already advanced.";
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("requiring Validated"), std::string::npos);
    EXPECT_NE(message.find("workspace state is already Validated"),
              std::string::npos);
  }
}

} // namespace
} // namespace pegium::workspace
