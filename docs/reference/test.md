# Test helpers

`cursiv::test` builds test documents and checks diagnostics and editor
features. Link it to your test target, and also link `cursiv::lsp` to use the
diagnostic and editor checks. [Test your language](../learn/testing.md) shows
complete test programs.

## Build a test document

| Helper | Result |
|---|---|
| `parseHelper(services, text, options)` | A `std::shared_ptr<Document>`, added to the workspace and built |
| `parseDocument(services, text, options)` | The same; throws `ExpectationError` if no AST was built |
| `validationHelper(services, text, options)` | A `ValidationResult` with the document and its diagnostics |

These helpers resolve references and validate; set `options.validation = false`
to skip validation. To parse only, call `services.parser->parse(text)`.

## Check diagnostics

Include `cursiv/test/ValidationHelper.hpp` and
`cursiv/test/ExpectDiagnosticOptions.hpp`. With the Recipe `language` from
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
| `expectNoIssues` | No diagnostics |
| `expectIssue` | A diagnostic matching the supplied options |

A message is exact text or a predicate on `std::string_view`. Options can
restrict the match to an offset, length, range, diagnostic code, node or
property; every option you set must match.

## Check editor features

Editor helpers take text with markers: `<|>` for the caret and `<|text|>` for
an expected range.

| Helper | Checks |
|---|---|
| `expectCompletion` | Suggested text at the caret |
| `expectHover` | Documentation |
| `expectGoToDefinition` | A reference's target |
| `expectFindReferences` | Uses of a declaration |
| `expectRename` | Rename edits and rejected names |
| `expectHighlight` | Related occurrences |
| `expectSymbols` | The document outline |
| `expectWorkspaceSymbols` | Workspace symbol search |
| `expectFoldings` | Foldable ranges |
| `expectSemanticToken` | Semantic highlighting |
| `expectFormatting` | Formatted text |

For example, with `cursiv/test/ExpectedHover.hpp` and the Recipe LSP
`services` created as in [Check editor features](../learn/testing.md#check-editor-features):

```cpp
(void)ct::expectHover(services,
    {.text = "/** The gram unit. */\nunit gram\n"
             "ingredient Flour in g<|>ram\n",
     .hover = "The gram unit.",
     .disposeAfterCheck = true});
```

## Clean up documents

Test documents stay in the workspace until you remove them; destroying a
result does not remove them. When tests share a workspace, remove them so
that the declarations of one case do not leak into the next:

- call `dispose()` on a `ValidationResult`, or on the `Disposable` that an
  editor helper returns (or set `disposeAfterCheck = true`);
- call `clearDocuments(services)` for `parseHelper` and `parseDocument`
  documents.

## Assertions and the test exit code

Include `cursiv/test/Check.hpp`:

| Function or macro | Purpose |
|---|---|
| `CHECK(condition)` | Report a failed assertion with its file and line |
| `count_expectations()` | Count failed expectations instead of throwing |
| `report("name")` | Print the result and return a test exit code |

Call `count_expectations()` first to report every failure, and return
`report("name")` from `main` so CTest sees the result.

## Recovery and fuzz

`cursiv/test/RecoveryLane.hpp` compares how the parser repairs broken
documents with a reference file, `REFERENCE.txt`. Run the test with `--write`
to regenerate it. If the LL and LR parsers repair a document differently,
copy `REFERENCE.txt` to `REFERENCE-ll.txt` or `REFERENCE-lr.txt` beside it;
`--write` then updates that copy for that parser.

`cursiv/fuzz/FuzzRun.hpp` generates documents, checks their trees against the
grammar and reports grammar coverage. Each example language has both tests in
its `tests/` directory.
