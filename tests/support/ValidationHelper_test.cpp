// The public test helpers, on the workspace fixture language: what each helper
// builds and compares, the marker scanner, and the verdict hook.

#include "cursiv/ValidationHelper.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/AstAssertions.hpp"
#include "cursiv/TestWorkspace.hpp"
#include "workspace/core/WorkspaceGrammar.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/CodeActionResult.hpp"
#include "cursiv/DecodedSemanticTokens.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/ExpectDiagnosticOptions.hpp"
#include "cursiv/ExpectFormatting.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ExpectedCompletion.hpp"
#include "cursiv/ExpectedFindReferences.hpp"
#include "cursiv/ExpectedFoldings.hpp"
#include "cursiv/ExpectedGoToDefinition.hpp"
#include "cursiv/ExpectedHighlight.hpp"
#include "cursiv/ExpectedHover.hpp"
#include "cursiv/ExpectedRename.hpp"
#include "cursiv/ExpectedSelectionRanges.hpp"
#include "cursiv/ExpectedSymbols.hpp"
#include "cursiv/ExpectedWorkspaceSymbols.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/ReplacedIndices.hpp"
#include "cursiv/TextDocumentParams.hpp"
#include "cursiv/TextPredicate.hpp"
#include "cursiv/VirtualFileSystemProvider.hpp"
#include "cursiv/actions/CodeActionProvider.hpp"
#include "cursiv/completion/CompletionProvider.hpp"
#include "cursiv/navigation/DeclarationProvider.hpp"
#include "cursiv/navigation/DefinitionProvider.hpp"
#include "cursiv/navigation/ImplementationProvider.hpp"
#include "cursiv/navigation/TypeDefinitionProvider.hpp"
#include "cursiv/navigation/RenameProvider.hpp"
#include "cursiv/semantic/InlayHintProvider.hpp"
#include "cursiv/semantic/SemanticTokenProvider.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "support/Throws.hpp"
#include "support/WorkspaceModule.hpp"
#include "support/World.hpp"
#include "workspace/lsp/LspModule.hpp"
#include "workspace/lsp/LspServices.hpp"

namespace {

namespace ct = cursiv::test;

struct World {
  ws::WorkspaceLanguage services = ws::create_lsp_services();
  ws::WorkspaceSharedServices& shared = *services.shared;
  ws::WorkspaceServices& language = services.language;
};

// A check runs to the end, or it throws: both are answers, and the test says
// which it wanted.
[[nodiscard]] bool passes(cursiv::FunctionRef<void()> check) {
  return !support::throws<ct::ExpectationError>(check);
}

//  0         1
//  0123456789012345
constexpr std::string_view kModel =
    "datatype String\n"  // 0..15
    "entity Person {\n"  // 16..31
    "  name: String\n"   // 32..46
    "}\n";               // 47..48

void the_markers_become_offsets() {
  const ct::ReplacedIndices marked =
      ct::replaceIndices({.text = "a<|>b<|cd|>e"});
  CHECK(marked.output == "abcde");
  CHECK(marked.indices.size() == 1 && marked.indices[0] == 1);
  CHECK(marked.ranges.size() == 1);
  CHECK(marked.ranges[0].first == 2 && marked.ranges[0].second == 4);

  // An index marker inside a range: `<|>` outranks `<|` at one position, so
  // the caret is not read as a second range opening.
  const ct::ReplacedIndices two = ct::replaceIndices({.text = "<|ab<|>cd|>"});
  CHECK(two.output == "abcd");
  CHECK(two.indices.size() == 1 && two.indices[0] == 2);
  CHECK(two.ranges.size() == 1);
  CHECK(two.ranges[0].first == 0 && two.ranges[0].second == 4);

  // Nested ranges come back sorted by where they open, innermost closed first.
  const ct::ReplacedIndices sorted =
      ct::replaceIndices({.text = "<|a<|b|>c|>"});
  CHECK(sorted.output == "abc");
  CHECK(sorted.ranges.size() == 2);
  CHECK(sorted.ranges[0].first == 0 && sorted.ranges[0].second == 3);
  CHECK(sorted.ranges[1].first == 1 && sorted.ranges[1].second == 2);

  // The text on each side of a marker never joins into a marker of its own:
  // `a<` and `|b` are text, and the caret stands between them.
  const ct::ReplacedIndices joined = ct::replaceIndices({.text = "a<<|>|b"});
  CHECK(joined.output == "a<|b");
  CHECK(joined.indices.size() == 1 && joined.indices[0] == 2);
  CHECK(joined.ranges.empty());
  const ct::ReplacedIndices inside =
      ct::replaceIndices({.text = "x<|y<<|>|>z"});
  CHECK(inside.output == "xy<z");
  CHECK(inside.indices.size() == 1 && inside.indices[0] == 3);
  CHECK(inside.ranges.size() == 1);
  CHECK(inside.ranges[0].first == 1 && inside.ranges[0].second == 3);

  // Each marker is looked for past the last one, once: a text of many ranges
  // and no caret costs its length, not its length at every range.
  std::string many;
  for (unsigned i = 0; i < 100000; ++i) many += "<|x|>";
  const auto begin = std::chrono::steady_clock::now();
  const ct::ReplacedIndices ranges = ct::replaceIndices({.text = many});
  CHECK(ranges.ranges.size() == 100000 && ranges.indices.empty());
  CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(2));

  // The markers are the caller's: a text that spells `<|>` needs other ones.
  const ct::ReplacedIndices custom =
      ct::replaceIndices({.text = "x[!]y", .indexMarker = "[!]"});
  CHECK(custom.output == "xy");
  CHECK(custom.indices.size() == 1 && custom.indices[0] == 1);

  // A range end with no open range, or a range left open, is a fault of the
  // expectation, not a range from the start of the text or no range at all.
  CHECK(!passes([] { (void)ct::replaceIndices({.text = "ab|>c"}); }));
  CHECK(!passes([] { (void)ct::replaceIndices({.text = "a<|b<|c|>"}); }));

  // So is a marker the check asks for and the text lacks: every helper says
  // which one it wanted and how many the text holds, and reads nothing past
  // them.
  World world;
  CHECK(!passes([&] {
    (void)ct::expectHover(world.language, {.text = "datatype S<|>tring\n",
                                           .index = 1,
                                           .disposeAfterCheck = true});
  }));
  std::vector<std::pair<std::string, std::string>> reported;
  ct::setExpectFunction([&reported](std::string_view actual,
                                    std::string_view expected,
                                    std::string_view) {
    if (actual != expected) reported.emplace_back(expected, actual);
  });
  const auto saysMissing = [&reported](std::string_view asked,
                                       std::string_view held) {
    const bool said =
        std::ranges::find(reported, std::pair<std::string, std::string>(
                                        asked, held)) != reported.end();
    reported.clear();
    return said;
  };
  constexpr std::string_view kOne = "datatype <|S<|>tring|>\n";
  constexpr std::string_view kUse =
      "datatype <|String|>\nentity Person {\n  name: <|>String\n}\n";

  (void)ct::expectHover(
      world.language,
      {.text = std::string(kOne), .index = 1, .disposeAfterCheck = true});
  CHECK(saysMissing("index marker 1", "1 index marker"));
  (void)ct::expectCompletion(
      world.language,
      ct::ExpectedCompletionItems{
          .text = std::string(kOne), .index = 2, .disposeAfterCheck = true});
  CHECK(saysMissing("index marker 2", "1 index marker"));
  (void)ct::expectRename(world.language, {.text = std::string(kOne),
                                          .index = 1,
                                          .newName = "Text",
                                          .disposeAfterCheck = true});
  CHECK(saysMissing("index marker 1", "1 index marker"));
  (void)ct::expectHighlight(
      world.language,
      {.text = std::string(kOne), .index = 1, .disposeAfterCheck = true});
  CHECK(saysMissing("index marker 1", "1 index marker"));
  (void)ct::expectHighlight(world.language, {.text = std::string(kOne),
                                             .rangeIndex = std::size_t{1},
                                             .disposeAfterCheck = true});
  CHECK(saysMissing("range 1", "1 range"));
  (void)ct::expectHighlight(world.language,
                            {.text = std::string(kOne),
                             .rangeIndex = std::vector<std::size_t>{3},
                             .disposeAfterCheck = true});
  CHECK(saysMissing("range 3", "1 range"));
  (void)ct::expectGoToDefinition(world.language, {.text = std::string(kUse),
                                                  .index = 1,
                                                  .rangeIndex = std::size_t{0},
                                                  .disposeAfterCheck = true});
  CHECK(saysMissing("index marker 1", "1 index marker"));
  (void)ct::expectGoToDefinition(world.language, {.text = std::string(kUse),
                                                  .rangeIndex = std::size_t{1},
                                                  .disposeAfterCheck = true});
  CHECK(saysMissing("range 1", "1 range"));
  (void)ct::expectGoToDefinition(world.language,
                                 {.text = std::string(kUse),
                                  .rangeIndex = std::vector<std::size_t>{2},
                                  .disposeAfterCheck = true});
  CHECK(saysMissing("range 2", "1 range"));
  ct::expectSemanticToken(
      ct::highlightHelper(world.language, "datatype String\n"),
      {.rangeIndex = 0});
  CHECK(saysMissing("range 0", "0 ranges"));
  ct::setExpectFunction({});
}

// A helper-built document is in the store, named after the counter, and the
// sweep takes it back out.
void a_parsed_document_joins_and_leaves_the_workspace() {
  World world;
  const std::shared_ptr<cursiv::Document> first =
      ct::parseHelper(world.language, std::string(kModel));
  CHECK(first->uri.ends_with(".ws"));
  CHECK(world.shared.documents->hasDocument(first->uri));
  CHECK(first->hasAst());

  // A second text does not collide with the first.
  const std::shared_ptr<cursiv::Document> second =
      ct::parseDocument(world.language, "datatype Int\n");
  CHECK(second->uri != first->uri);
  CHECK(world.shared.documents->all().size() == 2);

  // ...and a named URI is honoured.
  ct::ParseHelperOptions named;
  named.documentUri = "file:///w/named.ws";
  const std::shared_ptr<cursiv::Document> third =
      ct::parseHelper(world.language, "datatype Bool\n", named);
  CHECK(third->uri == "file:///w/named.ws");

  const std::shared_ptr<cursiv::Document> one[] = {third};
  ct::clearDocuments(world.shared, one);
  CHECK(!world.shared.documents->hasDocument("file:///w/named.ws"));
  CHECK(world.shared.documents->all().size() == 2);

  ct::clearDocuments(world.language);
  CHECK(world.shared.documents->all().empty());

  ct::ValidationHelperOptions reused;
  reused.documentUri = named.documentUri;
  auto old = ct::validationHelper(world.language, "datatype Old\n", reused);
  ct::clearDocuments(world.shared);
  auto current = ct::parseDocument(world.language, "datatype New\n", named);
  old.dispose();
  CHECK(world.shared.documents->getDocument(current->uri) == current);
}

// A helper-built document is the given language's, though a later one claims
// its extension; an extension written without its dot still names the file.
void a_parsed_document_is_the_given_languages() {
  World world;
  cursiv::CoreServices& other = support::add_language<cursiv::CoreServices>(
      world.shared, {"other", {"ws"}, {}}, support::create_workspace_parser(),
      support::create_workspace_reflection());
  const std::shared_ptr<cursiv::Document> mine =
      ct::parseHelper(world.language, "datatype Int\n");
  CHECK(mine->language == &world.language);
  const std::shared_ptr<cursiv::Document> theirs =
      ct::parseHelper(other, "datatype Int\n");
  CHECK(theirs->language == &other && theirs->uri.ends_with(".ws"));
  ct::clearDocuments(world.language);
}

void the_request_envelopes_carry_the_document() {
  World world;
  const std::shared_ptr<cursiv::Document> document =
      ct::parseDocument(world.language, std::string(kModel));

  CHECK(!ct::textDocumentParams(*document).uri.toString().empty());
  const lsp::TextDocumentPositionParams at =
      ct::textDocumentPositionParams(*document, 40);
  CHECK(at.position.line == 2 && at.position.character == 8);
  const lsp::ReferenceParams refs = ct::referenceParams(*document, 40, true);
  CHECK(refs.context.includeDeclaration);
  CHECK(refs.position.line == 2);
}

// The two checks of this language, and the filters that pick one of them out.
void the_findings_are_filtered_by_what_the_check_names() {
  World world;
  ct::ValidationResult result =
      ct::validationHelper(world.language, "datatype string\n");
  CHECK(!result.diagnostics.empty());

  CHECK(passes([&] {
    ct::expectWarning(result, "Type name should start with a capital.");
  }));
  CHECK(passes([&] { ct::expectIssue(result); }));
  // ...and the same finding is not an error.
  CHECK(!passes([&] {
    ct::expectError(result, "Type name should start with a capital.");
  }));

  // The anchor is the `name` member: the offsets pick it out, the range does
  // too, and a neighbouring span picks out nothing.
  CHECK(passes([&] {
    ct::expectWarning(result, "Type name should start with a capital.",
                      {.offset = 9, .length = 6});
  }));
  CHECK(
      passes([&] { ct::expectNoIssues(result, {.offset = 0, .length = 8}); }));
  CHECK(passes([&] {
    ct::expectIssue(result, {.range = lsp::Range{{0, 9}, {0, 15}}});
  }));

  // The predicate arm of the message, and a message nothing carries.
  CHECK(passes([&] { ct::expectWarning(result, ct::contains("capital")); }));
  CHECK(
      passes([&] { ct::expectWarning(result, ct::matches("^Type .*[.]$")); }));
  CHECK(!passes([&] { ct::expectWarning(result, "no such finding"); }));

  // Filtering on the AST node itself, and on the member that was written.
  CHECK(passes([&] {
    ct::expectIssue(result,
                    {.node = result.document->parseResult.nodes().front(),
                     .property = "name"});
  }));

  CHECK(!passes([&] {
    ct::expectIssue(result,
                    {.node = result.document->parseResult.nodes().front(),
                     .property = "name",
                     .propertyIndex = 1});
  }));
  CHECK(!passes([&] { ct::expectIssue(result, {.propertyIndex = 0}); }));
  CHECK(!passes([&] { ct::expectIssue(result, {.length = 1}); }));
  const auto other = ct::parseDocument(world.language, "datatype string\n");
  CHECK(!passes([&] {
    ct::expectIssue(result, {.node = other->parseResult.nodes().front(),
                             .property = "name"});
  }));
  CHECK(!passes([&] { ct::expectIssue(result, {.property = "name"}); }));
  const std::shared_ptr<cursiv::Document> foreign[] = {other};
  ct::clearDocuments(world.shared, foreign);
  cursiv::Diagnostic eof;
  const auto end = static_cast<cursiv::TextOffset>(
      result.document->textDocument().getText().size());
  eof.range = {end, end};
  eof.source = "my-language";
  eof.tags = {cursiv::DiagnosticTag::Unnecessary,
              cursiv::DiagnosticTag::Deprecated};
  result.diagnostics = {eof};
  CHECK(passes([&] {
    ct::expectIssue(result,
                    {.source = "my-language",
                     .tags = std::vector{cursiv::DiagnosticTag::Deprecated,
                                         cursiv::DiagnosticTag::Unnecessary}});
  }));
  CHECK(!passes(
      [&] { ct::expectIssue(result, {.source = "another-language"}); }));
  CHECK(!passes([&] {
    ct::expectIssue(result,
                    {.tags = std::vector{cursiv::DiagnosticTag::Unnecessary}});
  }));
  CHECK(ct::filterByOptions(result, {.offset = end + 1}).empty());
  CHECK(ct::filterByOptions(result, {.offset = end, .length = 1}).empty());
  result.dispose();
  CHECK(world.shared.documents->all().empty());
}

// A clean document has nothing to say, and the parse half is a finding like
// any other -- which is what `failOnParsingErrors` refuses.
void a_clean_document_answers_no_issue() {
  World world;
  const ct::ValidationResult clean =
      ct::validationHelper(world.language, std::string(kModel));
  CHECK(passes([&] { ct::expectNoIssues(clean); }));

  ct::ValidationHelperOptions strict;
  strict.failOnParsingErrors = true;
  CHECK(passes([&] {
    (void)ct::validationHelper(world.language, std::string(kModel), strict);
  }));
  CHECK(!passes([&] {
    (void)ct::validationHelper(world.language, "entity A { x  String }\n",
                               strict);
  }));
  strict.validation = false;
  CHECK(!passes([&] {
    (void)ct::validationHelper(world.language, "entity A { x  String }\n",
                               strict);
  }));
}

// The printer, the range comparison and the payload comparison: the three
// small answers every message above is built out of.
void the_small_answers_are_what_a_message_is_built_from() {
  CHECK(ct::rangeToString(lsp::Range{{1, 2}, {3, 4}}) == "1:2--3:4");
  CHECK(
      ct::isRangeEqual(lsp::Range{{1, 2}, {3, 4}}, lsp::Range{{1, 2}, {3, 4}}));
  CHECK(!ct::isRangeEqual(lsp::Range{{1, 2}, {3, 4}},
                          lsp::Range{{1, 2}, {3, 5}}));

  cursiv::Diagnostic one;
  one.range = {4, 9};
  one.message = "said";
  const cursiv::Diagnostic set[] = {one};
  CHECK(ct::printDiagnostics(set) == "offset 4..9: said");

  CHECK(ct::isDiagnosticDataEqual(1, 1.0));
  CHECK(ct::isDiagnosticDataEqual(-0.0, 0));
  CHECK(!ct::isDiagnosticDataEqual(true, 1));
  CHECK(!ct::isDiagnosticDataEqual(1.5, 1));
  CHECK(!ct::isDiagnosticDataEqual(std::int64_t{9007199254740993}, 0x1p53));
  CHECK(!ct::isDiagnosticDataEqual("\x80", "\x81"));
  CHECK(!ct::isDiagnosticDataEqual(std::int64_t{0x7fffffffffffffff}, 0x1p63));

  // The expected payload is a subset: a key it does not name is free.
  cursiv::JsonValue::Object whole;
  whole["code"] = cursiv::JsonValue("x");
  whole["extra"] = cursiv::JsonValue(1);
  cursiv::JsonValue::Object part;
  part["code"] = cursiv::JsonValue("x");
  CHECK(ct::isDiagnosticDataEqual(cursiv::JsonValue(whole),
                                  cursiv::JsonValue(part)));
  cursiv::JsonValue::Object other;
  other["code"] = cursiv::JsonValue("y");
  CHECK(!ct::isDiagnosticDataEqual(cursiv::JsonValue(whole),
                                   cursiv::JsonValue(other)));
  // At every depth, and only between two objects.
  cursiv::JsonValue::Object inner;
  inner["a"] = cursiv::JsonValue(1);
  inner["b"] = cursiv::JsonValue(2);
  whole["nested"] = cursiv::JsonValue(inner);
  inner.erase("b");
  part["nested"] = cursiv::JsonValue(inner);
  CHECK(ct::isDiagnosticDataEqual(cursiv::JsonValue(whole),
                                  cursiv::JsonValue(part)));
  CHECK(!ct::isDiagnosticDataEqual(cursiv::JsonValue(whole),
                                   cursiv::JsonValue("x")));
  // An array by position: the leading elements, each read the same way.
  cursiv::JsonValue::Object both;
  both["a"] = cursiv::JsonValue(1);
  both["b"] = cursiv::JsonValue(2);
  const cursiv::JsonValue held(
      cursiv::JsonValue::Array{cursiv::JsonValue(both), cursiv::JsonValue(3)});
  CHECK(ct::isDiagnosticDataEqual(
      held,
      cursiv::JsonValue(cursiv::JsonValue::Array{cursiv::JsonValue(inner)})));
  CHECK(!ct::isDiagnosticDataEqual(
      held, cursiv::JsonValue(cursiv::JsonValue::Array{cursiv::JsonValue(3)})));
  CHECK(!ct::isDiagnosticDataEqual(
      held, cursiv::JsonValue(cursiv::JsonValue::Array{cursiv::JsonValue(both),
                                                       cursiv::JsonValue(3),
                                                       cursiv::JsonValue(4)})));
}

void the_caret_reaches_its_declaration() {
  World world;
  const ct::ExpectedGoToDefinition input{.text =
                                             "datatype <|String|>\n"
                                             "entity Person {\n"
                                             "  name: <|>String\n"
                                             "}\n",
                                         .index = 0,
                                         .rangeIndex = std::size_t{0},
                                         .disposeAfterCheck = true};
  CHECK(passes([&] { (void)ct::expectGoToDefinition(world.language, input); }));
  CHECK(world.shared.documents->all().empty());

  // The same span in another document is not the declaration.
  struct Elsewhere final : cursiv::DefinitionProvider {
    const cursiv::DefinitionProvider& inner;
    explicit Elsewhere(const cursiv::DefinitionProvider& provider)
        : inner(provider) {}
    [[nodiscard]] std::optional<std::vector<lsp::LocationLink>> getDefinition(
        const cursiv::Document& document, lsp::Position position,
        const cursiv::CancellationToken& cancel) const override {
      auto links = inner.getDefinition(document, position, cancel);
      if (links.has_value())
        for (lsp::LocationLink& link : *links)
          link.targetUri = lsp::Uri::parse("file:///elsewhere.ws");
      return links;
    }
  };
  const std::unique_ptr<const cursiv::DefinitionProvider> language =
      std::move(world.language.definitionProvider);
  world.language.definitionProvider = std::make_unique<Elsewhere>(*language);
  CHECK(
      !passes([&] { (void)ct::expectGoToDefinition(world.language, input); }));
  CHECK(world.shared.documents->all().empty());
}

void every_written_name_is_found() {
  World withoutCaret;
  CHECK(!passes([&] {
    (void)ct::expectFindReferences(
        withoutCaret.language,
        {.text = "datatype <|String|>\n", .disposeAfterCheck = true});
  }));
  World world;
  const ct::ExpectedFindReferences input{.text =
                                             "datatype <|>String\n"
                                             "entity Person {\n"
                                             "  name: <|<|>String|>\n"
                                             "}\n",
                                         .includeDeclaration = false,
                                         .disposeAfterCheck = true};
  CHECK(passes([&] { (void)ct::expectFindReferences(world.language, input); }));
}

void the_symbol_is_painted_where_it_stands() {
  World world;
  const ct::ExpectedHighlight input{.text =
                                        "datatype <|String|>\n"
                                        "entity Person {\n"
                                        "  name: <|><|String|>\n"
                                        "}\n",
                                    .index = 0,
                                    .disposeAfterCheck = true};
  CHECK(passes([&] { (void)ct::expectHighlight(world.language, input); }));
}

// Rename at the caret: the edits cover exactly the marked spans with the new
// name.
void the_name_is_rewritten_where_it_is_used() {
  World world;
  const ct::ExpectedRename input{.text =
                                     "datatype <|S<|>tring|>\n"
                                     "entity Person {\n"
                                     "  name: <|String|>\n"
                                     "}\n",
                                 .newName = "Text",
                                 .disposeAfterCheck = true};
  CHECK(passes([&] { (void)ct::expectRename(world.language, input); }));
}

void the_outline_names_what_the_document_declares() {
  World world;
  std::vector<std::string> names;
  const ct::ExpectedSymbolsCallback seen{
      .text = std::string(kModel),
      .assertion = [&names](std::span<const lsp::DocumentSymbol> symbols) {
        for (const lsp::DocumentSymbol& symbol : symbols)
          names.push_back(symbol.name);
      }};
  CHECK(passes([&] { (void)ct::expectSymbols(world.language, seen); }));
  CHECK(names.size() == 2 && names[0] == "String" && names[1] == "Person");

  CHECK(passes([&] {
    (void)ct::expectSymbols(
        world.language,
        ct::ExpectedSymbolsList{.text = std::string(kModel),
                                .expectedSymbols = {"String", "Person"},
                                .disposeAfterCheck = true});
  }));

  // Markers are taken out before the parse, and one left unpaired fails.
  CHECK(passes([&] {
    (void)ct::expectSymbols(
        world.language,
        ct::ExpectedSymbolsList{
            .text = "datatype <|String|>\nentity <|>Person {\n}\n",
            .expectedSymbols = {"String", "Person"},
            .disposeAfterCheck = true});
  }));
  CHECK(!passes([&] {
    (void)ct::expectSymbols(
        world.language, ct::ExpectedSymbolsList{.text = "datatype <|String\n",
                                                .expectedSymbols = {"String"},
                                                .disposeAfterCheck = true});
  }));

  // ...and a list that is wrong is refused rather than tolerated, even where
  // its names joined spell the received ones.
  for (const std::vector<std::string>& wrong :
       {std::vector<std::string>{"String"},
        std::vector<std::string>{"String, Person"},
        std::vector<std::string>{"", "String", "Person"}})
    CHECK(!passes([&] {
      (void)ct::expectSymbols(
          world.language, ct::ExpectedSymbolsList{.text = std::string(kModel),
                                                  .expectedSymbols = wrong,
                                                  .disposeAfterCheck = true});
    }));
}

void the_multi_line_record_is_foldable() {
  World world;
  // One fold: the entity's three lines, the closing brace left visible. The
  // document's own root is never one.
  const ct::ExpectedFoldings input{.text =
                                       "datatype String\n"
                                       "<|entity Person {\n"
                                       "  name: String|>\n"
                                       "}\n",
                                   .disposeAfterCheck = true};
  CHECK(passes([&] { (void)ct::expectFoldings(world.language, input); }));
}

void the_caret_is_offered_what_may_stand_there() {
  World world;
  bool offersString = false;
  const ct::ExpectedCompletionCallback offered{
      .text =
          "datatype String\n"
          "entity Person {\n"
          "  name: <|>\n"
          "}\n",
      .assertion = [&offersString](const lsp::CompletionList& list) {
        for (const lsp::CompletionItem& item : list.items)
          if (item.label == "String") offersString = true;
      }};
  CHECK(passes([&] { (void)ct::expectCompletion(world.language, offered); }));
  CHECK(offersString);

  // The list arm, on a caret whose answer is one word.
  CHECK(passes([&] {
    (void)ct::expectCompletion(
        world.language,
        ct::ExpectedCompletionItems{
            .text = "entity Person { name: String }\ndatatyp<|>\n",
            .expectedItems = {"datatype"},
            .disposeAfterCheck = true});
  }));
  // A list one item longer is refused, even an empty item.
  CHECK(!passes([&] {
    (void)ct::expectCompletion(
        world.language,
        ct::ExpectedCompletionItems{
            .text = "entity Person { name: String }\ndatatyp<|>\n",
            .expectedItems = {"", "datatype"},
            .disposeAfterCheck = true});
  }));

  struct Ranked final : cursiv::CompletionProvider {
    lsp::CompletionList getCompletion(
        const cursiv::Document&, const lsp::CompletionParams&,
        const cursiv::CancellationToken&) const override {
      lsp::CompletionList list;
      for (const auto* label : {"z", "explicit", "a"})
        list.items.emplace_back().label = label;
      list.items[1].sortText = "middle";
      return list;
    }
  };
  world.language.completionProvider = std::make_unique<Ranked>();
  CHECK(passes([&] {
    (void)ct::expectCompletion(
        world.language,
        ct::ExpectedCompletionItems{.text = "datatype <|>String",
                                    .expectedItems = {"a", "explicit", "z"},
                                    .disposeAfterCheck = true});
  }));
}

void the_prose_of_a_declaration_reaches_its_use() {
  World world;
  constexpr std::string_view kDocumented =
      "/** The text. */\n"
      "datatype String\n"
      "entity Person {\n"
      "  name: <|>String\n"
      "}\n";
  CHECK(passes([&] {
    (void)ct::expectHover(world.language, {.text = std::string(kDocumented),
                                           .hover = "The text."});
  }));

  CHECK(passes([&] {
    (void)ct::expectHover(world.language, {.text = std::string(kDocumented),
                                           .hoverMatches = ct::contains("text"),
                                           .disposeAfterCheck = true});
  }));

  // Without a hover, expecting any hover text fails.
  constexpr std::string_view kBare = "datatype String\n<|>\n";
  CHECK(passes([&] {
    (void)ct::expectHover(world.language, {.text = std::string(kBare),
                                           .disposeAfterCheck = true});
  }));
  CHECK(!passes([&] {
    (void)ct::expectHover(world.language, {.text = std::string(kBare),
                                           .hover = "<none>",
                                           .disposeAfterCheck = true});
  }));
}

void a_reference_is_coloured_as_one() {
  World world;
  const ct::DecodedSemanticTokensWithRanges highlighted =
      ct::highlightHelper(world.language,
                          "datatype String\n"
                          "entity Person {\n"
                          "  name: <|String|>\n"
                          "}\n");
  CHECK(!highlighted.tokens.empty());
  CHECK(highlighted.ranges.size() == 1);
  CHECK(passes([&] {
    ct::expectSemanticToken(highlighted, {.tokenType = "variable"});
  }));

  // ...and not a keyword: the check can fail.
  CHECK(!passes(
      [&] { ct::expectSemanticToken(highlighted, {.tokenType = "keyword"}); }));
}

void the_formatter_reaches_the_shape_the_language_wants() {
  World world;
  lsp::FormattingOptions twoSpaces;
  twoSpaces.insertSpaces = true;
  twoSpaces.tabSize = 2;
  const ct::ExpectFormatting input{.before =
                                       "entity   Person{\n"
                                       "name   :String\n"
                                       "}\n"
                                       "datatype     String\n",
                                   .after =
                                       "entity Person {\n"
                                       "  name: String\n"
                                       "}\n"
                                       "datatype String\n",
                                   .options = twoSpaces,
                                   .disposeAfterCheck = true};
  CHECK(passes([&] { (void)ct::expectFormatting(world.language, input); }));
  auto onType = input;
  onType.onType = "}";
  CHECK(!passes([&] { (void)ct::expectFormatting(world.language, onType); }));
  onType.before += "<|>";
  onType.range.emplace();
  CHECK(!passes([&] { (void)ct::expectFormatting(world.language, onType); }));
  CHECK(world.shared.documents->all().empty());
}

void the_workspace_query_reads_the_index() {
  World world;
  (void)ct::parseDocument(world.language, "entity Person {\n}\n");

  CHECK(passes([&] {
    ct::expectWorkspaceSymbols(
        world.shared, ct::ExpectedWorkspaceSymbolsList{
                          .query = "Person", .expectedSymbols = {"Person"}});
  }));
  CHECK(!passes([&] {
    ct::expectWorkspaceSymbols(
        world.shared,
        ct::ExpectedWorkspaceSymbolsList{.query = "Person",
                                         .expectedSymbols = {"", "Person"}});
  }));

  CHECK(passes([&] {
    ct::expectWorkspaceSymbols(
        world.shared,
        ct::ExpectedWorkspaceSymbolsList{.query = "Person",
                                         .expectedSymbols = {"Person"},
                                         .match = ct::SymbolMatch::ExactSet});
  }));
  CHECK(!passes([&] {
    ct::expectWorkspaceSymbols(
        world.shared, ct::ExpectedWorkspaceSymbolsList{
                          .query = "Person",
                          .match = ct::SymbolMatch::Contains,
                          .kinds = {{"Person", lsp::SymbolKind::File}}});
  }));
  std::size_t count = 1;
  ct::expectWorkspaceSymbols(
      world.shared,
      ct::ExpectedWorkspaceSymbolsCallback{
          .query = "zzz-nothing-declares-this",
          .assertion = [&count](std::span<const lsp::WorkspaceSymbol> symbols) {
            count = symbols.size();
          }});
  CHECK(count == 0);
}

// The insertion goes after the last token shifted, so the trivia stays
// where the author put it. The fix is checked under the input's URI; the
// input then stays in the workspace until disposed.
void a_repair_is_offered_and_really_fixes() {
  constexpr std::string_view kBroken =
      "datatype String\nentity A { x  String }\n";
  constexpr std::string_view kFixed =
      "datatype String\nentity A { x:  String }\n";
  for (const std::string_view uri : {"", "file:///w/fixed.ws"}) {
    World world;
    ct::ParseHelperOptions options;
    options.documentUri = uri;
    bool ran = false;
    try {
      ct::CodeActionResult result =
          ct::testCodeAction(world.language, std::string(kBroken),
                             "parse-repaired", kFixed, options);
      ran = true;
      CHECK(result.action.has_value());
      CHECK(!result.diagnosticsAll.empty());
      CHECK(cursiv::dataCode(result.diagnosticRelevant) == "parse-repaired");
      CHECK(result.document->hasAst());
      CHECK(result.document->textDocument().getText() == kBroken);
      CHECK(uri.empty() || result.document->uri == uri);
      const auto held = world.shared.documents->all();
      CHECK(held.size() == 1);
      if (held.size() == 1) CHECK(held.front() == result.document);
      result.dispose();
    } catch (const std::exception&) {
    }
    CHECK(ran);
    CHECK(world.shared.documents->all().empty());
  }
}

// A check that throws removes the document it built: its caller never
// receives the cleanup.
void a_failed_check_leaves_no_document() {
  World world;
  CHECK(!passes([&] {
    (void)ct::expectHover(world.language,
                          {.text = "datatype S<|>tring\n", .hover = "nothing"});
  }));
  CHECK(world.shared.documents->all().empty());

  ct::ValidationHelperOptions strict;
  strict.failOnParsingErrors = true;
  CHECK(!passes([&] {
    (void)ct::validationHelper(world.language, "entity A { x  String }\n",
                               strict);
  }));
  CHECK(world.shared.documents->all().empty());

  CHECK(!passes([&] {
    (void)ct::testCodeAction(world.language,
                             "datatype String\nentity A { x  String }\n",
                             "parse-repaired", "not the fix");
  }));
  CHECK(world.shared.documents->all().empty());

  // A provider that throws, whatever it throws.
  struct Throwing final : cursiv::CodeActionProvider {
    [[nodiscard]] std::vector<lsp::CodeAction> getCodeActions(
        const cursiv::Document&, const lsp::CodeActionParams&,
        const cursiv::CancellationToken&) const override {
      throw std::runtime_error("provider");
    }
  };
  world.language.codeActionProvider = std::make_unique<Throwing>();
  CHECK(support::throws<std::runtime_error>([&] {
    (void)ct::testCodeAction(world.language,
                             "datatype String\nentity A { x  String }\n",
                             "parse-repaired", std::nullopt);
  }));
  CHECK(world.shared.documents->all().empty());
}

// The language's own actions, their edits filed under another document; with
// `alsoHere`, also under their own, while the other document gets an edit of
// its own.
struct Refiled final : cursiv::CodeActionProvider {
  const cursiv::CodeActionProvider& inner;
  bool alsoHere = false;

  Refiled(const cursiv::CodeActionProvider& provider, bool here)
      : inner(provider), alsoHere(here) {}

  [[nodiscard]] std::vector<lsp::CodeAction> getCodeActions(
      const cursiv::Document& document, const lsp::CodeActionParams& params,
      const cursiv::CancellationToken& cancel) const override {
    std::vector<lsp::CodeAction> actions =
        inner.getCodeActions(document, params, cancel);
    const lsp::DocumentUri here = lsp::Uri::parse(document.uri);
    const lsp::DocumentUri other = lsp::Uri::parse("file:///elsewhere.ws");
    for (lsp::CodeAction& action : actions) {
      if (!action.edit.has_value() || !action.edit->changes.has_value())
        continue;
      auto& changes = *action.edit->changes;
      std::vector<lsp::TextEdit> own = changes[here];
      changes.clear();
      if (alsoHere) {
        changes[here] = own;
        own = {lsp::TextEdit{.range = {}, .newText = "datatype Elsewhere\n"}};
      }
      changes[other] = std::move(own);
    }
    return actions;
  }
};

// Only the edits filed under the diagnosed document change its text.
void a_repair_is_judged_on_its_own_document() {
  constexpr std::string_view kBroken =
      "datatype String\nentity A { x  String }\n";
  constexpr std::string_view kFixed =
      "datatype String\nentity A { x:  String }\n";
  for (const bool alsoHere : {false, true}) {
    World world;
    const std::unique_ptr<const cursiv::CodeActionProvider> language =
        std::move(world.language.codeActionProvider);
    world.language.codeActionProvider =
        std::make_unique<Refiled>(*language, alsoHere);
    const bool fixed = passes([&] {
      ct::CodeActionResult result = ct::testCodeAction(
          world.language, std::string(kBroken), "parse-repaired", kFixed);
      result.dispose();
    });
    CHECK(fixed == alsoHere);
  }
}

// `edit` as a client that reads versioned edits receives it.
void as_document_changes(lsp::WorkspaceEdit& edit) {
  if (!edit.changes.has_value()) return;
  std::vector<std::variant<lsp::TextDocumentEdit, lsp::CreateFile,
                           lsp::RenameFile, lsp::DeleteFile>>
      moved;
  for (auto& [uri, rows] : *edit.changes) {
    lsp::TextDocumentEdit one;
    one.textDocument.uri = uri;
    for (lsp::TextEdit& row : rows) one.edits.emplace_back(std::move(row));
    moved.emplace_back(std::move(one));
  }
  edit.changes.reset();
  edit.documentChanges = std::move(moved);
}

// A provider may file its edits as `documentChanges`: the checks read them.
void an_edit_filed_as_document_changes_is_read() {
  struct Actions final : cursiv::CodeActionProvider {
    const cursiv::CodeActionProvider& inner;
    explicit Actions(const cursiv::CodeActionProvider& provider)
        : inner(provider) {}
    [[nodiscard]] std::vector<lsp::CodeAction> getCodeActions(
        const cursiv::Document& document, const lsp::CodeActionParams& params,
        const cursiv::CancellationToken& cancel) const override {
      CHECK(params.context.triggerKind == lsp::CodeActionTriggerKind::Invoked);
      CHECK(params.context.diagnostics.size() == 1 &&
            params.context.diagnostics.front().data.has_value() &&
            ct::isRangeEqual(params.context.diagnostics.front().range,
                             params.range));
      std::vector<lsp::CodeAction> actions =
          inner.getCodeActions(document, params, cancel);
      for (lsp::CodeAction& action : actions)
        if (action.edit.has_value()) as_document_changes(*action.edit);
      return actions;
    }
  };
  struct Rename final : cursiv::RenameProvider {
    const cursiv::RenameProvider& inner;
    explicit Rename(const cursiv::RenameProvider& provider) : inner(provider) {}
    [[nodiscard]] std::optional<lsp::Range> prepareRename(
        const cursiv::Document& document, lsp::Position position,
        const cursiv::CancellationToken& cancel) const override {
      return inner.prepareRename(document, position, cancel);
    }
    [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
        const cursiv::Document& document, lsp::Position position,
        std::string_view newName,
        const cursiv::CancellationToken& cancel) const override {
      auto edit = inner.rename(document, position, newName, cancel);
      if (edit.has_value()) as_document_changes(*edit);
      return edit;
    }
  };
  lsp::WorkspaceEdit samePosition;
  const auto uri = lsp::Uri::parse("file:///same.ws");
  auto& inserted = samePosition.changes.emplace()[uri];
  for (int i = 0; i < 32; ++i)
    inserted.emplace_back().newText = std::to_string(i);
  const auto ordered = ct::detail::editsFor(samePosition, uri.toString());
  CHECK(ordered.size() == inserted.size());
  for (std::size_t i = 0; i < ordered.size(); ++i)
    CHECK(ordered[i].newText == std::to_string(i));

  World world;
  const std::unique_ptr<const cursiv::CodeActionProvider> actions =
      std::move(world.language.codeActionProvider);
  world.language.codeActionProvider = std::make_unique<Actions>(*actions);
  const std::unique_ptr<const cursiv::RenameProvider> rename =
      std::move(world.language.renameProvider);
  world.language.renameProvider = std::make_unique<Rename>(*rename);
  CHECK(passes([&] {
    ct::CodeActionResult result = ct::testCodeAction(
        world.language, "datatype String\nentity A { x  String }\n",
        "parse-repaired", "datatype String\nentity A { x:  String }\n");
    result.dispose();
  }));
  CHECK(passes([&] {
    (void)ct::expectRename(world.language, {.text = "datatype <|S<|>tring|>\n"
                                                    "entity Person {\n"
                                                    "  name: <|String|>\n"
                                                    "}\n",
                                            .newName = "Text",
                                            .disposeAfterCheck = true});
  }));
}

void the_virtual_file_system_answers_like_a_disk() {
  ct::VirtualFileSystemProvider files;
  files.insert("file:///w/a.ws", "datatype String\n");
  files.insert("file:///w/sub/b.ws", "datatype Int\n");

  CHECK(files.exists("file:///w/a.ws"));
  CHECK(files.exists("file:///w/sub"));
  CHECK(!files.exists("file:///w/missing.ws"));
  CHECK(files.readFile("file:///w/a.ws") == "datatype String\n");
  CHECK(files.readBinary("file:///w/sub/b.ws").size() == 13);

  CHECK(files.stat("file:///w/a.ws").isFile);
  CHECK(files.stat("file:///w/sub").isDirectory);
  CHECK(support::throws<cursiv::FileSystemError>([&] {
    (void)files.stat("file:///w/missing.ws");
  }));

  const std::vector<cursiv::FileSystemNode> under =
      files.readDirectory("file:///w");
  CHECK(under.size() == 2);
  if (under.size() == 2) {
    CHECK(under[0].isFile && under[0].uri == "file:///w/a.ws");
    CHECK(under[1].isDirectory && under[1].uri == "file:///w/sub");
  }

  files.insert("file:///w/sub.ws", "sibling");
  files.erase("");
  const auto sorted = files.readDirectory("file:///w/");
  CHECK(sorted.size() == 3 && sorted[1].uri == "file:///w/sub");
  files.erase("file:///w/sub/");
  CHECK(files.readFile("file:///w/sub.ws") == "sibling");
  files.erase("file:///w/sub.ws");
  CHECK(!files.exists("file:///w/sub/b.ws"));
  CHECK(files.readDirectory("file:///w").size() == 1);
  CHECK(support::throws<cursiv::FileSystemError>(
      [&] { (void)files.readDirectory("file:///w/a.ws"); }));
}

// A foreign harness receives the verdicts: with a hook installed, a false
// expectation is reported rather than thrown, so a framework that counts
// failures counts this one.
void an_installed_hook_receives_the_verdict() {
  std::vector<std::string> reported;
  ct::setExpectFunction([&reported](std::string_view actual,
                                    std::string_view expected,
                                    std::string_view message) {
    if (actual != expected) reported.emplace_back(message);
  });

  World world;
  const ct::ExpectedSymbolsList wrong{.text = std::string(kModel),
                                      .expectedSymbols = {"NotDeclared"},
                                      .disposeAfterCheck = true};
  // No throw, whatever the answer: the hook decided.
  (void)ct::expectSymbols(world.language, wrong);
  CHECK(!reported.empty());

  ct::setExpectFunction({});
  CHECK(!passes([&] { (void)ct::expectSymbols(world.language, wrong); }));
}

void named_markers_link_and_rename_a_workspace() {
  World world;
  ct::TestWorkspace fixture(world.shared);
  constexpr auto lib = "file:///w/types.ws", use = "file:///w/usage.ws",
                 more = "file:///w/other.ws";
  auto docs =
      fixture.parse({{use, "entity É { f: <|def><|refs:String|> }\n"},
                     {more, "entity G { y: <|refs:String|> }\n"},
                     {lib, "datatype <|refs><|rename><|def:String|>\n"}});
  CHECK(docs.size() == 3 && docs.front()->uri == use);
  CHECK(!docs.front()->hasLinkingErrors());
  CHECK(passes([&] { fixture.expectDefinition(use, "def"); }));
  CHECK(passes([&] { fixture.expectReferences(lib, "refs"); }));
  CHECK(!passes([&] { fixture.expectReferences(lib, "refs", true); }));
  CHECK(!passes([&] { fixture.expectDefinition(use, "unknown"); }));
  const auto& file = fixture.at(use);
  CHECK(file.lspRange("refs").start.character == 14);
  using Ref = cursiv::reference<ws::ast::Type>;
  const auto* ref = ct::findReference<ws::Workspace, Ref>(
      *file.document, "String", file.range("refs"));
  CHECK(ct::requireFound(ref, "String reference").target() ==
        fixture.at(lib).findNode<ws::ast::DataType>("String"));
  CHECK(!passes([&] {
    (void)ct::requireFound(file.findNode<ws::ast::DataType>(),
                            "local datatype");
  }));
  CHECK(file.findNode<ws::ast::Feature>({}, file.range("refs").begin) !=
        nullptr);
  auto tokens = file.semanticTokens("refs");
  CHECK(passes([&] {
    ct::expectSemanticToken(tokens,
                            {.tokenType = "variable", .tokenModifiers = 0});
  }));
  CHECK(!passes([&] {
    ct::expectSemanticToken(tokens,
                            {.tokenType = "variable", .tokenModifiers = 7});
  }));
  struct VersionedRename final : cursiv::RenameProvider {
    const cursiv::RenameProvider& inner;
    int version = -1;
    explicit VersionedRename(const cursiv::RenameProvider& p) : inner(p) {}
    std::optional<lsp::Range> prepareRename(
        const cursiv::Document& d, lsp::Position p,
        const cursiv::CancellationToken& token) const override {
      return inner.prepareRename(d, p, token);
    }
    std::optional<lsp::WorkspaceEdit> rename(
        const cursiv::Document& d, lsp::Position p, std::string_view name,
        const cursiv::CancellationToken& token) const override {
      auto edit = inner.rename(d, p, name, token);
      if (edit) {
        as_document_changes(*edit);
        std::get<lsp::TextDocumentEdit>(edit->documentChanges->back())
            .textDocument.version = version;
      }
      return edit;
    }
  };
  auto original = std::move(world.language.renameProvider);
  auto versioned = std::make_unique<VersionedRename>(*original);
  auto* control = versioned.get();
  world.language.renameProvider = std::move(versioned);
  CHECK(!passes([&] { fixture.rename(lib, "rename", "LongerName"); }));
  CHECK(docs[0]->textDocument().version() == 0 &&
        docs[2]->textDocument().version() == 0);
  control->version = 0;
  fixture.rename(lib, "rename", "LongerName");
  CHECK(docs[0]->textDocument().getText() == "entity É { f: LongerName }\n");
  CHECK(docs[1]->textDocument().getText() == "entity G { y: LongerName }\n");
  CHECK(docs[2]->textDocument().getText() == "datatype LongerName\n");
  CHECK(!docs[0]->hasLinkingErrors());
  CHECK(passes([&] {
    fixture.expectDefinition(use, "def");
    fixture.expectReferences(lib, "refs");
  }));
  CHECK(passes(
      [&] { ct::expectSemanticToken(tokens, {.tokenType = "variable"}); }));
  CHECK(!passes([&] { (void)fixture.at(lib).range("absent"); }));
  world.language.renameProvider = std::move(original);
  fixture.rename(lib, "rename", "Y");
  CHECK(docs[1]->textDocument().getText() == "entity G { y: Y }\n");
  CHECK(passes([&] { fixture.expectDefinition(use, "def"); }));
  fixture.erase(more);
  CHECK(!world.shared.documents->hasDocument(more));
  CHECK(passes([&] { fixture.expectReferences(lib, "refs"); }));
  auto outside = ct::parseDocument(world.language, "datatype Outside\n");
  CHECK(support::throws<cursiv::DocumentStoreError>([&] {
    fixture.parse({{"file:///w/added.ws", "datatype New"},
                   {outside->uri, "datatype Duplicate"}});
  }));
  CHECK(!world.shared.documents->hasDocument("file:///w/added.ws"));
  fixture.clear();
  fixture.clear();
  CHECK(world.shared.documents->getDocument(outside->uri) == outside);
  fixture.attach({outside, {{"name", {9, 16}, false}}});
  CHECK(fixture.at(outside->uri).range("name").end == 16);
  fixture.clear();
  CHECK(world.shared.documents->all().empty());
}

void named_markers_and_ast_values_are_checked() {
  ct::MarkerSyntax syntax{.rangeStart = "[",
                          .rangeEnd = "]",
                          .indexStart = "[[",
                          .indexEnd = "]]",
                          .delimiter = "="};
  std::vector<ct::NamedMarker> marks;
  CHECK(ct::extractMarkers("[a=é][[p]][b]", marks, syntax) == "éb");
  CHECK(marks.size() == 3 && marks[0].label == "a" && marks[0].range.end == 2 &&
        marks[1].caret && marks[1].range.begin == 2 && marks[2].label == "b");
  World world;
  ct::TestWorkspace fixture(world.shared);
  auto trees = fixture.parse(
      {{"file:///w/a.ws", "package a { datatype X entity E { many f: X } }"},
       {"file:///w/b.ws", "package b { datatype X entity E { many f: X } }"},
       {"file:///w/c.ws", "package c { datatype X entity E { f: X } }"}});
  const auto get = [&](std::size_t i) {
    return fixture.at(trees[i]->uri).findNode<ws::ast::Entity>();
  };
  CHECK(get(0) && get(1) && get(2));
  const auto* one = fixture.at(trees[0]->uri).findNode<ws::ast::DataType>();
  const auto* two = fixture.at(trees[1]->uri).findNode<ws::ast::DataType>();
  CHECK(passes([&] { ct::expectEqualAst<ws::Workspace>(*one, *two); }));
  CHECK(
      !passes([&] { ct::expectEqualAst<ws::Workspace>(*get(0), *get(2)); }));
  CHECK(!passes([&] { ct::expectEqualAst<ws::Workspace>(*one, *get(0)); }));
  CHECK(!passes([&] {
    ct::expectEqualAst<ws::Workspace>(
        *one, *two, [](const auto&, const auto&) { return false; });
  }));
  ws::ast::DataType first, second;
  first.name = "One";
  second.name = "Another";
  CHECK(!passes([&] { ct::expectEqualAst<ws::Workspace>(first, second); }));
  ws::ast::Feature plain{}, many{};
  many.many = true;
  CHECK(!passes([&] { ct::expectEqualAst<ws::Workspace>(plain, many); }));
  cursiv::CancellationTokenSource stop;
  stop.request_stop();
  CHECK(support::throws<cursiv::OperationCancelled>([&] {
    fixture.parse({{"file:///w/cancelled.ws", "datatype X"}}, {}, {},
                  stop.get_token());
  }));
  CHECK(!world.shared.documents->hasDocument("file:///w/cancelled.ws"));
  auto editorDocs = fixture.parse(
      {{"file:///w/doc.ws",
        "datatype <|hi><|hi:Str|>\n<|fold:entity Local {\n  f: Str|>\n}\n"},
       {"file:///w/symbol.ws", "entity Container { <|sym:f: Str|> }\n"},
       {"file:///w/assist.ws", "entity Edit { f: <|assist> }\n"}});
  const auto& local = fixture.at(editorDocs.front()->uri);
  const auto items =
      fixture.at("file:///w/assist.ws").completion("assist").items;
  CHECK(std::ranges::find(items, "Str", &lsp::CompletionItem::label) !=
        items.end());
  CHECK(passes([&] { local.expectFolding("fold"); }));
  // 'Str' occurs twice locally and in another file. A missing local highlight
  // fails.
  CHECK(!passes([&] { local.expectHighlights("hi"); }));
  const auto sym = fixture.at("file:///w/symbol.ws").symbol("sym");
  CHECK(sym && sym->name == "f");
  CHECK(passes([&] {
    ct::expectNoIssues(*editorDocs[1],
                       {.severity = cursiv::DiagnosticSeverity::Error});
  }));
  fixture.parse({{"file:///w/full.ws", "<|full:datatype Unique|>  <|blank>\n"},
                 {"file:///w/read.ws", "entity Read { f: <|full>Unique }"}});
  CHECK(passes(
      [&] { fixture.expectDefinition("file:///w/read.ws", "full", true); }));
  CHECK(
      !passes([&] { fixture.expectDefinition("file:///w/read.ws", "full"); }));
  CHECK(!fixture.at("file:///w/full.ws").hover("blank"));
  fixture.parse({{"file:///w/uses.ws",
                  "entity More { a: <|uses:Unique|> b: <|uses:Unique|> }"}});
  const auto painted = fixture.at("file:///w/uses.ws").semanticTokens("uses");
  CHECK(painted.ranges.size() == 2);
  CHECK(passes([&] { painted.expectAll("variable", 0); }));
  CHECK(!passes([&] { painted.expectAll("variable", 1); }));
  auto missing = painted;
  missing.ranges.back().second++;
  CHECK(!passes([&] { missing.expectAll("variable", 0); }));
  fixture.clear();
}

void marked_editor_requests_reach_their_providers() {
  World world;
  ct::TestWorkspace fixture(world.shared);
  constexpr auto use = "file:///w/use.ws", targets = "file:///w/targets.ws";
  fixture.parse(
      {{use, "entity É { f: <|decl><|impl><|type><|hint:A|> }<|none>"},
       {targets,
        "datatype <|decl:A|>\n<|impl:datatype B|>\ndatatype <|type:C|>"}});
  CHECK(!passes([&] { fixture.expectDeclaration(use, "none"); }));
  struct Destinations final : cursiv::DeclarationProvider,
                              cursiv::ImplementationProvider,
                              cursiv::TypeDefinitionProvider {
    using Links = std::optional<std::vector<lsp::LocationLink>>;
    static Links link(lsp::Position position, lsp::Uint line,
                      const cursiv::CancellationToken& token) {
      cursiv::throwIfCancelled(token);
      if (position.line != 0 || position.character != 14) return {};
      return std::vector<lsp::LocationLink>{
          {.targetUri = lsp::Uri::parse("file:///w/targets.ws"),
           .targetRange = {{line, 0}, {line, 10}},
           .targetSelectionRange = {{line, 9}, {line, 10}}}};
    }
    Links getDeclaration(
        const cursiv::Document&, lsp::Position p,
        const cursiv::CancellationToken& token) const override {
      return link(p, 0, token);
    }
    Links getImplementation(
        const cursiv::Document&, lsp::Position p,
        const cursiv::CancellationToken& token) const override {
      return link(p, 1, token);
    }
    Links getTypeDefinition(
        const cursiv::Document&, lsp::Position p,
        const cursiv::CancellationToken& token) const override {
      return link(p, 2, token);
    }
  };
  world.language.declarationProvider = std::make_unique<Destinations>();
  world.language.implementationProvider = std::make_unique<Destinations>();
  world.language.typeDefinitionProvider = std::make_unique<Destinations>();
  fixture.expectDeclaration(use, "decl");
  fixture.expectImplementation(use, "impl", true);
  fixture.expectTypeDefinition(use, "type");
  fixture.expectTypeDefinition(use, "none");
  CHECK(!passes([&] { fixture.expectDeclaration(use, "type"); }));
  CHECK(!passes([&] { fixture.expectImplementation(use, "impl"); }));

  struct Hints final : cursiv::InlayHintProvider {
    std::vector<lsp::InlayHint> getInlayHints(
        const cursiv::Document& doc, lsp::Range range,
        const cursiv::CancellationToken& token) const override {
      cursiv::throwIfCancelled(token);
      return {{.position = range.end,
               .label = std::string(
                   doc.textDocument().getText(cursiv::fromLsp(range)))}};
    }
  };
  const auto& file = fixture.at(use);
  CHECK(!passes([&] { (void)file.inlayHints("hint"); }));
  world.language.inlayHintProvider = std::make_unique<Hints>();
  const auto hints = file.inlayHints("hint");
  CHECK(hints.size() == 1 && hints.front().position.character == 15 &&
        std::get<std::string>(hints.front().label) == "A");
  cursiv::CancellationTokenSource stop;
  stop.request_stop();
  CHECK(support::throws<cursiv::OperationCancelled>(
      [&] { (void)file.inlayHints("hint", stop.get_token()); }));
  fixture.clear();
}

void a_selection_requires_the_exact_chain() {
  World world;
  for (const auto* text : {"datatype <|A<|>|>", "<|datatype <|A<|>|>|><||>",
                           "datatype A<|><|>", "datatype A"}) {
    CHECK(!passes([&] {
      (void)ct::expectSelectionRanges(world.language, {.text = text});
    }));
    CHECK(world.shared.documents->all().empty());
  }
  (void)ct::expectSelectionRanges(
      world.language, {.text = "<|<|>|>", .disposeAfterCheck = true});
}

void a_disposable_runs_once() {
  int runs = 0;
  ct::Disposable once = ct::Disposable::create([&runs] { ++runs; });
  once.dispose();
  once.dispose();
  CHECK(runs == 1);
}

}  // namespace

int main() {
  marked_editor_requests_reach_their_providers();
  a_selection_requires_the_exact_chain();
  named_markers_link_and_rename_a_workspace();
  named_markers_and_ast_values_are_checked();
  the_markers_become_offsets();
  a_parsed_document_joins_and_leaves_the_workspace();
  a_parsed_document_is_the_given_languages();
  the_request_envelopes_carry_the_document();
  the_findings_are_filtered_by_what_the_check_names();
  a_clean_document_answers_no_issue();
  the_small_answers_are_what_a_message_is_built_from();
  the_caret_reaches_its_declaration();
  every_written_name_is_found();
  the_symbol_is_painted_where_it_stands();
  the_name_is_rewritten_where_it_is_used();
  the_outline_names_what_the_document_declares();
  the_multi_line_record_is_foldable();
  the_caret_is_offered_what_may_stand_there();
  the_prose_of_a_declaration_reaches_its_use();
  a_reference_is_coloured_as_one();
  the_formatter_reaches_the_shape_the_language_wants();
  the_workspace_query_reads_the_index();
  a_repair_is_offered_and_really_fixes();
  a_repair_is_judged_on_its_own_document();
  an_edit_filed_as_document_changes_is_read();
  a_failed_check_leaves_no_document();
  the_virtual_file_system_answers_like_a_disk();
  an_installed_hook_receives_the_verdict();
  a_disposable_runs_once();
  return cursiv::test::report("test_support");
}
