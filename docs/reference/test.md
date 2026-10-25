# Test helpers

`cursiv::test` provides document-building helpers, diagnostic assertions and
expectations for editor features. See [Test your language](../learn/testing.md)
for complete test programs using the Recipe language.

Link your test target to `cursiv::test`. Add `cursiv::lsp` when using the
diagnostic or editor expectation headers:

```cmake
target_link_libraries(mylang_test
    PRIVATE mylangCore cursiv::test cursiv::lsp)
```

## Build a test document

| Helper | Result |
|---|---|
| `parseHelper(services, text, options)` | A registered and built `std::shared_ptr<Document>` |
| `parseDocument(services, text, options)` | The same; throws `ExpectationError` if no AST was built |
| `validationHelper(services, text, options)` | A `ValidationResult` containing the document and diagnostics |

All three resolve references and run validation by default. Set
`options.validation = false` to skip validation. For parsing alone, call
`services.parser->parse(text)`.

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

Also test a valid document with `ct::expectNoIssues(result)`.

| Helper | Checks |
|---|---|
| `expectError` | An error with the expected message |
| `expectWarning` | A warning with the expected message |
| `expectNoIssues` | No diagnostics |
| `expectIssue` | A diagnostic matching the supplied options |

Messages can be exact text or a predicate accepting `std::string_view`.
Options can restrict the match to an offset, length, range, diagnostic code,
node or property. Every supplied option must match.

## Clean up documents

A `ValidationResult` has an explicit `dispose()` method that removes its
document from the workspace. Call it when reusing the same workspace across
tests, so declarations from one case do not affect the next.
Destruction alone does not call `dispose()`.

For documents returned by `parseHelper` or `parseDocument`, use
`clearDocuments(services)` to remove the workspace's test documents.

## Check editor features

Editor helpers take text containing markers: `<|>` marks a caret, and
`<|text|>` marks an expected range.

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

For example, include `cursiv/test/ExpectedHover.hpp` to check hover. Given a
Recipe LSP services container named `services`:

```cpp
(void)ct::expectHover(services,
    {.text = "/** The gram unit. */\nunit gram\n"
             "ingredient Flour in g<|>ram\n",
     .hover = "The gram unit.",
     .disposeAfterCheck = true});
```

These helpers return a `Disposable`: call `dispose()` to remove the document,
or set `disposeAfterCheck = true` to remove it after the check.

## Assertions and the test exit code

Include `cursiv/test/Check.hpp`:

| Function or macro | Purpose |
|---|---|
| `CHECK(condition)` | Report a failed assertion with its file and line |
| `count_expectations()` | Count failed expectations rather than throwing |
| `report("name")` | Print the result and return a test exit code |

Call `count_expectations()` before the `expect*` helpers if you want a test
to report all failures. Return `report("name")` from `main` so CTest sees the
result.

## Recovery and fuzz

For larger languages, add recovery and generated-input tests:

`cursiv/test/RecoveryLane.hpp` compares repairs of invalid documents with
a reference file you can regenerate using `--write`. It independently checks
that repaired input is reported as repaired.

`cursiv/fuzz/FuzzRun.hpp` generates documents and checks their trees against
the grammar directives, reporting coverage. Each complete example language
includes both kinds of test in its `tests/` directory.
