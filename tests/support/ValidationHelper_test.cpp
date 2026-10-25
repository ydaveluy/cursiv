// The public test module, exercised through the one shipped language that has
// every service: what each helper builds, what it compares, and the two things
// nothing else pins -- the marker scanner that turns a text into offsets, and
// the hook that lets a foreign harness receive the verdicts.

#include "cursiv/test/ValidationHelper.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/CodeActionResult.hpp"
#include "cursiv/test/DecodedSemanticTokens.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ExpectDiagnosticOptions.hpp"
#include "cursiv/test/ExpectFormatting.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectationError.hpp"
#include "cursiv/test/ExpectedBase.hpp"
#include "cursiv/test/ExpectedCompletion.hpp"
#include "cursiv/test/ExpectedFindReferences.hpp"
#include "cursiv/test/ExpectedFoldings.hpp"
#include "cursiv/test/ExpectedGoToDefinition.hpp"
#include "cursiv/test/ExpectedHighlight.hpp"
#include "cursiv/test/ExpectedHover.hpp"
#include "cursiv/test/ExpectedRename.hpp"
#include "cursiv/test/ExpectedSymbols.hpp"
#include "cursiv/test/ExpectedWorkspaceSymbols.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "cursiv/test/RangeUtils.hpp"
#include "cursiv/test/ReplacedIndices.hpp"
#include "cursiv/test/TextDocumentParams.hpp"
#include "cursiv/test/TextPredicate.hpp"
#include "cursiv/test/VirtualFileSystemProvider.hpp"
#include "workspace/lsp/LspModule.hpp"
#include "workspace/lsp/LspServices.hpp"

namespace {

namespace ct = cursiv::test;

struct World {
  ws::WorkspaceLanguage services = ws::create_lsp_services();
  ws::WorkspaceSharedServices& shared = *services.shared;
  ws::WorkspaceServices& language = services.language;
};

// A check runs to the end, or it throws: both are answers, and the lane names
// which it wanted.
template <class F>
[[nodiscard]] bool passes(F&& check) {
  try {
    check();
  } catch (const ct::ExpectationError&) {
    return false;
  }
  return true;
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

  // The markers are the caller's: a text that spells `<|>` needs other ones.
  const ct::ReplacedIndices custom =
      ct::replaceIndices({.text = "x[!]y", .indexMarker = "[!]"});
  CHECK(custom.output == "xy");
  CHECK(custom.indices.size() == 1 && custom.indices[0] == 1);
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
    ct::expectIssue(
        result, {.node = result.document->parseResult.nodes.front(),
                 .property = "name"});
  }));

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
}

void every_written_name_is_found() {
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

// The rename door: the edit box opens, and the edits cover exactly the marked
// spans with the new name.
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

  // ...and a list that is wrong is refused rather than tolerated.
  CHECK(!passes([&] {
    (void)ct::expectSymbols(
        world.language, ct::ExpectedSymbolsList{.text = std::string(kModel),
                                                .expectedSymbols = {"String"},
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
    ct::expectSemanticToken(highlighted,
                            {.tokenType = cursiv::SemanticTokenType::Variable});
  }));

  // ...and it is not a keyword, which is what makes the check a check.
  CHECK(!passes([&] {
    ct::expectSemanticToken(highlighted,
                            {.tokenType = cursiv::SemanticTokenType::Keyword});
  }));
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
}

void the_workspace_query_reads_the_index() {
  World world;
  (void)ct::parseDocument(world.language, "entity Person {\n}\n");

  CHECK(passes([&] {
    ct::expectWorkspaceSymbols(
        world.shared, ct::ExpectedWorkspaceSymbolsList{
                          .query = "Person", .expectedSymbols = {"Person"}});
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

// The insertion lands after the last token shifted, not in front of the one
// that could not be, so the trivia between them stays where the author put it.
void a_repair_is_offered_and_really_fixes() {
  World world;
  ct::CodeActionResult result = ct::testCodeAction(
      world.language, "datatype String\nentity A { x  String }\n",
      "parse-repaired", "datatype String\nentity A { x:  String }\n");
  CHECK(result.action.has_value());
  CHECK(!result.diagnosticsAll.empty());
  CHECK(result.diagnosticRelevant.code.has_value());
  result.dispose();
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
  bool threw = false;
  try {
    (void)files.stat("file:///w/missing.ws");
  } catch (const cursiv::FileSystemError&) {
    threw = true;
  }
  CHECK(threw);

  const std::vector<cursiv::FileSystemNode> under =
      files.readDirectory("file:///w");
  CHECK(under.size() == 2);
  if (under.size() == 2) {
    CHECK(under[0].isFile && under[0].uri == "file:///w/a.ws");
    CHECK(under[1].isDirectory && under[1].uri == "file:///w/sub");
  }

  // Erasing a folder takes its subtree with it.
  files.erase("file:///w/sub");
  CHECK(!files.exists("file:///w/sub/b.ws"));
  CHECK(files.readDirectory("file:///w").size() == 1);
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

void a_disposable_runs_once() {
  int runs = 0;
  ct::Disposable once = ct::Disposable::create([&runs] { ++runs; });
  once.dispose();
  once.dispose();
  CHECK(runs == 1);
}

}  // namespace

int main() {
  the_markers_become_offsets();
  a_parsed_document_joins_and_leaves_the_workspace();
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
  the_virtual_file_system_answers_like_a_disk();
  an_installed_hook_receives_the_verdict();
  a_disposable_runs_once();
  return cursiv::test::report("test_support");
}
