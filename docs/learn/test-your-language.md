# Test Your Language

Pegium languages are ordinary C++, so you test them with ordinary C++ tests
(GoogleTest in the scaffold). The **public** `pegium::cli` helpers build a real
document through the full pipeline (parse → index → scopes → link → validate),
so you can assert on the resulting AST and diagnostics. This is exactly what the
generated `test/parsing_test.cpp` in a scaffolded project does, so the pattern
works for any downstream consumer. For more, see
[`examples/statemachine/tests/`](https://github.com/ydaveluy/pegium/tree/main/examples/statemachine/tests).

## A document-level test

Register your language on a shared runtime, build a sample file through the whole
pipeline, then assert on diagnostics and the typed AST:

```cpp
#include <gtest/gtest.h>

#include <pegium/cli/CliUtils.hpp>      // make_shared_services, build_document_from_path
#include <statemachine/core/CoreModule.hpp> // your createStatemachineCoreServices

TEST(MyLanguage, ParsesAndValidates) {
  // 1. Build a shared runtime and register your language on it (the same calls
  //    your cli/lsp main() makes).
  auto sharedServices =
      pegium::make_shared_services<statemachine::StatemachineSharedCoreServices>();
  auto &shared = *sharedServices;
  auto services = statemachine::createStatemachineCoreServices(shared);
  auto &langServices = *services;
  shared.serviceRegistry->registerServices(std::move(services));

  // 2. Build a source file through every phase. The scaffold injects an absolute
  //    path to a sample input via the PEGIUM_NEW_SAMPLE_PATH compile definition.
  auto document =
      pegium::build_document_from_path(PEGIUM_NEW_SAMPLE_PATH, langServices);
  ASSERT_NE(document, nullptr);

  // 3a. Assert on diagnostics (parse, linking, and validation problems).
  EXPECT_FALSE(pegium::has_error_diagnostics(*document));

  // 3b. Or assert on the typed AST.
  auto *model = pegium::ast_ptr_cast<statemachine::ast::Statemachine>(
      document->parseResult.value);
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->name, "Light");
}
```

What the helpers do (all part of the public `pegium::cli` surface):

- `make_shared_services()` creates the process-wide shared runtime — the same one
  your `main` would build.
- `createStatemachineCoreServices(shared)` / `registerServices(...)` is your own
  language registration (the function from `core/CoreModule.cpp`).
- `build_document_from_path(path, langServices)` reads the file and runs every
  build phase, so by the time it returns diagnostics are populated and references
  are linked. Pass `validation = false` to stop after linking.
- `has_error_diagnostics(*document)` is a quick pass/fail check; iterate
  `document->diagnostics` directly to inspect messages and ranges.
- `pegium::ast_ptr_cast<T>(document->parseResult.value)` returns the typed root
  node, or `nullptr` if the cast does not apply.

## Asserting on references and counts

The build pipeline links cross-references, so you can assert on them directly and
count diagnostics of a given kind:

```cpp
std::size_t duplicates = 0;
for (const auto &diagnostic : document->diagnostics) {
  if (diagnostic.message.find("Duplicate identifier name:") != std::string::npos) {
    ++duplicates;
  }
}
EXPECT_EQ(duplicates, 4u);
```

## Testing LSP features with `pegium::testing`

For validation and LSP-feature tests (completion, hover, go-to-definition,
references, highlight, formatting, symbols, folding), link the public
**`pegium::testing`** harness — pegium's equivalent of Langium's `langium/test`.
It builds a marked-up source string through the full pipeline and asserts on the
result.

A [scaffolded project](workflow/scaffold.md) already wires this up: with the LSP
server enabled it generates `test/lsp/` feature tests and links `pegium::testing`
for you. To add it to an existing test target yourself:

```cmake
target_link_libraries(my-lang-tests PRIVATE
    my-lang-core pegium::testing GTest::gtest_main)
```

Markers in the source: `<|>` is a cursor index (pick one with `.index`), and
`<| … |>` marks a range.

```cpp
#include <gtest/gtest.h>
#include <pegium/testing/Testing.hpp>
#include <statemachine/lsp/LspModule.hpp>

TEST(Statemachine, CompletesStateNames) {
  auto ws = pegium::testing::TestWorkspace::withShared<
      statemachine::StatemachineSharedServices>();
  ws.registerLanguage(statemachine::createStatemachineLspServices(
      ws.shared<statemachine::StatemachineSharedServices>()));

  pegium::testing::expectValidation(ws, "statemachine", {
      .text = "state idle",
      .diagnostics = {{.message = "should start with a capital letter"}},
  });

  pegium::testing::expectCompletion(ws, "statemachine", {
      .text = "statemachine TrafficLight\n"
              "events\n    next\n"
              "initialState Off\n"
              "state Off\n    next => <|>\nend\n"
              "state On\n    next => Off\nend\n",
      .expectedItems = {"Off", "On"},  // the transition target completes state names
  });
}
```

Available helpers: `parse`, `expectValidation`, `expectCompletion`,
`expectHover`, `expectGoToDefinition`, `expectFindReferences`,
`expectHighlight`, `expectFormatting`, `expectSymbols`,
`expectWorkspaceSymbols`, `expectFoldingRanges`. Each `Expected…` struct takes a
`.check` callback for assertions beyond the built-in matching. Construct one
`TestWorkspace` per test (or call `ws.clear()` between cases) for isolation.

### Granular validation assertions

Mirroring Langium's `validationHelper` + `expectError`/`expectWarning`, you can
parse once and run several focused assertions against the diagnostics:

```cpp
auto result = pegium::testing::validate(ws, "statemachine", source);
pegium::testing::expectNoIssues(result, {.severity = pegium::DiagnosticSeverity::Error});
pegium::testing::expectError(result, "Could not resolve reference");
```

`validate` returns a `ValidationResult { document, diagnostics }`. The
`expect{Error,Warning,Issue,NoIssues}` helpers take an optional `DiagnosticFilter`
(`severity`, `code`, `offset`, `range`, or a `predicate`) to narrow which
diagnostics are considered.

To test a quick-fix end-to-end (the analogue of Langium's `testCodeAction`),
`testCodeAction` parses the input, finds the single diagnostic carrying a given
code, applies the resulting `CodeAction`, and checks the fixed text:

```cpp
pegium::testing::testCodeAction(
    ws, "arithmetics", "module test\ndef test: 2 + 3;\n",
    std::string(arithmetics::validation::IssueCodes::ExpressionNormalizable),
    "module test\ndef test: 5;\n");
```

### Low-level probes and markers

The marker parser and `lsp::*Params` builders the `expect…` helpers are made of
are public in `<pegium/testing/LspProbe.hpp>` (the analogue of Langium's
`replaceIndices` / `textDocumentParams`), so you can build a custom probe:

```cpp
#include <pegium/testing/LspProbe.hpp>

const auto marked = pegium::testing::replaceIndices(source); // strips <|> and <| |>
auto document = pegium::testing::parse(ws, "statemachine", marked.output);
auto params = pegium::testing::completionParams(*document, marked.indices[0]);
auto completion = pegium::getCompletion(ws.shared(), params);
```

### Semantic tokens

For a language with a semantic-token provider, `highlight` decodes the tokens
(type names resolved through the provider legend) and `expectSemanticToken`
asserts one covers a marked range — the analogue of Langium's `highlightHelper` /
`expectSemanticToken`:

```cpp
const auto decoded =
    pegium::testing::highlight(ws, "statemachine", "statemachine <|Light|>\n…");
pegium::testing::expectSemanticToken(decoded, {.tokenType = "variable"});
```

### Driving a feature directly

For features the `expect…` helpers do not wrap (rename, prepare-rename, code
actions, …) or to assert on the raw protocol result, call the **headless feature
API** in `<pegium/lsp/services/LanguageServerFeatures.hpp>` — every LSP feature
as a free function over `ws.shared()` and an `lsp::*Params`, with no running
server. Build the params for a marked offset with
`document->textDocument().positionAt(offset)`:

```cpp
#include <pegium/lsp/services/LanguageServerFeatures.hpp>

auto document = pegium::testing::parse(ws, "domainmodel", "entity Foo {}");
const auto &text = document->textDocument();

::lsp::RenameParams params{};
params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
// A position is line/character; derive it from an offset (here, the start of
// "Foo"), or set params.position.line / .character directly.
params.position =
    text.positionAt(static_cast<pegium::TextOffset>(text.getText().find("Foo")));
params.newName = "Bar";

const auto edit = pegium::rename(ws.shared(), params);
```

## Iterating quickly on grammar shape

While you are still designing a grammar, the fastest loop is to build a tiny
snippet file through the same `build_document_from_path` helper and inspect
`document->parseResult.value`. References are only linked once the full pipeline
runs, so reach for the document-level test above when you need linking or
validation.

> For single-rule unit tests (no workspace), pegium's own suite uses an in-tree
> helper, `pegium::test::parse_rule_result` from
> `<pegium/core/TestRuleParser.hpp>`. It lives under `tests/`, is gated behind
> `BUILD_TESTING=ON`, and is **not** part of the stable public API — consumer
> projects test through the public `pegium::cli` and `pegium::testing` surfaces
> shown above.

## Related pages

- [Build a Language End-to-End](walkthrough.md)
- [Validation](../build-a-language/validation.md)
