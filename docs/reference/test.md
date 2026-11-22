# Test helpers

`cursiv::test` builds test documents and checks diagnostics and editor features.
Types use `PascalCase`. Free functions, member functions and fields use `camelCase`.
Link your test target to `cursiv::test` and add `cursiv::lsp` for diagnostic and
editor checks. [Test your language](../learn/testing.md) shows complete test programs.

## Build a test document

| Helper | Result |
|---|---|
| `parseHelper(services, text, options)` | A `std::shared_ptr<Document>`, added to the workspace and built |
| `parseDocument(services, text, options)` | The same; throws `ExpectationError` if no AST was built |
| `validationHelper(services, text, options)` | A `ValidationResult` with the document and its diagnostics |

These helpers resolve references and run validation by default. Set
`options.validation = false` to skip validation. Set
`failOnParsingErrors = true` on `validationHelper` to fail the test if parsing
fails or needs recovery, even with validation disabled. To parse only, call
`services.parser->parse(text)`.

## Test several files together

Link the test to `cursiv::test` and `cursiv::lsp` to use `TestWorkspace`.
It registers all files in a batch before building them. Each URI selects its
language, or a third `TestFile` field can specify a language ID.
In fixture text, `<|label>` marks a caret, `<|label:text|>` marks a range and
`<|word|>` uses `word` as both label and text. `MarkerSyntax` configures the
delimiters. Offsets are measured in bytes after removing the markers.

```cpp
#include "cursiv/TestWorkspace.hpp"

namespace ct = cursiv::test;
ct::TestWorkspace workspace(shared);
workspace.parse({
    {"file:///uses.ws", "entity E { field: <|definition><|uses:String|> }"},
    {"file:///types.ws", "datatype <|uses><|rename><|definition:String|>"}
});
workspace.expectDefinition("file:///uses.ws", "definition");
workspace.expectReferences("file:///types.ws", "uses");
workspace.rename("file:///types.ws", "rename", "Text");
workspace.expectDefinition("file:///uses.ws", "definition");
workspace.clear();
```

Use `expectDefinition`, `expectDeclaration`, `expectImplementation` or
`expectTypeDefinition` with one caret in the queried file. Mark every expected
target with the same label, in any file. The check compares the URI and the
selected name for each target. Pass `true` to compare the full target range.
With `expectReferences`, pass `true` to include the declaration. A rename
checks document versions, applies the provider's text edits in either LSP form,
updates the named ranges and rebuilds affected documents.

`workspace.at(uri)` gives the built document and its markers. `range(label)`
returns a byte span and `lspRange(label)` returns its UTF-16 range. Use
`completion`, `hover` and `signatureHelp` at a marker. Read the active
parameter from the signature result, or pass an `lsp::SignatureHelpContext`
to reproduce a retrigger.
`inlayHints(label)` returns the hints for the marked range. `symbol` searches
children by their full range. `expectHighlights` and `expectFolding` check
marked occurrences and folds. `file.semanticTokens("uses").expectAll("variable", 0)` checks the type
and modifier mask at every range labelled `uses`. The decoded snapshot survives
a subsequent rename.

Use `erase(uri)` to remove one file or `clear()` to remove the fixture's files.
`attach({document, markers})` gives the fixture the same responsibility for
an already registered document. Build and editor operations accept a
`CancellationToken`.

## Inspect trees and linking

`file.nodes<T>()` iterates nodes of a type, including its subtypes.
`file.findNode<T>("Name")` finds a named node and
`file.findNode<T>({}, file.range("site").begin)` finds the innermost matching
node at the marker. Check the document's build state before asserting on
linking, for example with `CHECK(doc.state >= cursiv::DocumentState::Linked &&
!doc.hasLinkingErrors())`.

Include `cursiv/AstAssertions.hpp` for typed reference lookup and tree comparison.

```cpp
const auto& ref = ct::requireFound(
    ct::findReference<Grammar, cursiv::reference<Type>>(doc, "Text", file.range("site")),
    "reference to Text");
CHECK(ref.target() == expectedDeclaration);
ct::expectEqualAst<Grammar>(*expectedRoot, *actualRoot);
```

AST comparison visits the grammar's bound C++ properties, including inherited
members, child order, reference spellings and resolved targets. Scalar values
use their C++ equality operator. An optional predicate compares additional
application properties on each pair of nodes.

## Check diagnostics

Include `cursiv/ValidationHelper.hpp` and
`cursiv/ExpectDiagnosticOptions.hpp`. With the Recipe `language` from
[Test your language](../learn/testing.md):

```cpp
namespace ct = cursiv::test;

auto result = ct::validationHelper(language.language,
    "unit gram\ningredient Flour in gram\n"
    "recipe Twice { use Flour 100 use Flour 200 }\n");
ct::expectError(result, "This ingredient is already used in this recipe.");
result.dispose();
```

| Helper | Checks |
|---|---|
| `expectError` | An error with the expected message |
| `expectWarning` | A warning with the expected message |
| `expectNoIssues` | No diagnostics matching the supplied filters, or none at all when called without filters |
| `expectIssue` | A diagnostic matching the supplied options |

A message is exact text or a predicate on `std::string_view`. Every supplied
filter must match. An offset and its length describe an exact byte span. A
property names a written occurrence on a node from the validated document,
with an optional zero-based index. Use `source` for an exact source name and
`tags` for an exact, unordered collection of tags. `expectIssue` and
`expectNoIssues` accept either a validation snapshot or a live document.
For a fixture marker use `ct::expectIssue(*file.document, {.range =
file.lspRange("site")})`.

`testCodeAction(services, text, code, fixed)`, from
`cursiv/CodeActionResult.hpp`, requires exactly one diagnostic with that code.
It passes the diagnostic to the provider in an explicit code-action request,
applies the single expected fix and checks both the resulting text and the
removal of the diagnostic. Pass `std::nullopt` as `fixed` when the provider
should offer no action. Call `dispose()` on the result to remove its document.

## Check editor features

Editor helpers take text with markers: `<|>` for the caret and `<|text|>` for
an expected range. A missing requested marker or an unpaired range fails the
check. `expectFindReferences` requires at least one caret and checks every
marked caret against the expected ranges.

| Helper | Checks |
|---|---|
| `expectCompletion` | Suggested text at the caret |
| `expectHover` | Documentation |
| `expectGoToDefinition` | A reference's target |
| `expectFindReferences` | Uses of a declaration |
| `expectRename` | Rename edits and rejected names |
| `expectHighlight` | Related occurrences |
| `expectSymbols` | The document outline |
| `expectWorkspaceSymbols` | Workspace symbol search in provider order, as an exact set or as a subset, with optional symbol kinds |
| `expectFoldings` | Foldable ranges |
| `expectSelectionRanges` | The exact sequence of selections from the caret to the outermost marked range |
| `expectSemanticToken` | Semantic highlighting: one token of a type, named as in the legend (`"keyword"`), over a marked range |
| `expectFormatting` | Formatted text after whole-document, range or on-type formatting |

For on-type formatting, put `<|>` just after the typed character in `before`
and set `onType` to that character. For selection ranges, include
`cursiv/ExpectedSelectionRanges.hpp` and nest `<|…|>` around each expected
step, as in this test for a language with `datatype` declarations.

```cpp
(void)ct::expectSelectionRanges(services,
    {.text = "<|datatype <|Na<|>me|>|>", .disposeAfterCheck = true});
```

`expectSemanticToken` reads the tokens that `highlightHelper` returns. Use
`rangeIndex` and `tokenType` to select a token, and `tokenModifiers` to require
an exact modifier mask, including zero.

For example, with `cursiv/ExpectedHover.hpp` and the Recipe LSP
`services` created as in [Check editor features](../learn/testing.md#check-editor-features):

```cpp
(void)ct::expectHover(services,
    {.text = "/** The gram unit. */\nunit gram\n"
             "ingredient Flour in g<|>ram\n",
     .hover = "The gram unit.",
     .disposeAfterCheck = true});
```

## Build requests and compare results

| Helper | Header | Result |
|---|---|---|
| `textDocumentParams(document)` | `cursiv/TextDocumentParams.hpp` | The document's `TextDocumentIdentifier` |
| `textDocumentPositionParams(document, offset)` | `cursiv/TextDocumentParams.hpp` | The document and the position at a byte offset |
| `referenceParams(document, offset, includeDeclaration)` | `cursiv/TextDocumentParams.hpp` | The parameters of a references request |
| `isRangeEqual(a, b)`, `rangeToString(range)` | `cursiv/RangeUtils.hpp` | Compare or print an `lsp::Range` |
| `isDiagnosticDataEqual(data, expected)` | `cursiv/ExpectDiagnosticOptions.hpp` | Whether `data` holds every value of `expected` |
| `printDiagnostics(diagnostics)` | `cursiv/ExpectDiagnosticOptions.hpp` | One line per diagnostic, with byte offsets |

## Clean up documents

Test documents stay in the workspace until you remove them; destroying a
result does not remove them, but a helper that throws removes the document it
added. When tests share a workspace, remove them so that the declarations of
one case do not leak into the next:

- call `dispose()` on a `ValidationResult`, or on the `Disposable` that an
  editor helper returns (or set `disposeAfterCheck = true`);
- for `parseHelper` and `parseDocument`, pass the documents to remove to
  `clearDocuments(services, documents)`. Use `clearDocuments(services)` to
  clear the entire workspace.

## Assertions and the test exit code

Include `cursiv/Check.hpp`:

| Function or macro | Purpose |
|---|---|
| `CHECK(condition)` | Report a failed assertion with its file and line |
| `countExpectations()` | Count failed expectations instead of throwing |
| `report("name")` | Print the result and return a test exit code |

Call `countExpectations()` first to report every failure, and return
`report("name")` from `main` so CTest sees the result.

## Recovery and fuzz

`cursiv/RecoveryLane.hpp` compares how the parser repairs broken
documents with a reference file, `REFERENCE.txt`. The test target defines
`CURSIV_RECOVERY_CORPUS` as the folder that holds them, as
`examples/cmake/Tests.cmake` does. Run the test with `--write` to regenerate
the reference.

`cursiv/FuzzRun.hpp` generates documents, checks their trees against the
grammar and reports grammar coverage. Each example language has both tests in
its `tests/` directory.
