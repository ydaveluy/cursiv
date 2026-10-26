# Test your language

The scaffold includes a test that parses the sample and checks its abstract
syntax tree (AST).
As your language grows, add tests in this order:

1. A valid sample parses and produces the expected AST.
2. Each validation reports an invalid case and accepts a valid one.
3. Cross references resolve, and missing targets produce diagnostics.
4. Custom editor providers return the expected results.

The examples below use the Recipe language from the
[tutorial](walkthrough.md).

```bash
ctest --test-dir build --output-on-failure
```

## Check the parse result

In the generated `tests/grammar_test.cpp`, keep the setup and update the AST
assertions when you change the language. Check both the parse result and the
diagnostics before reading the tree:

```cpp title="tests/grammar_test.cpp"
const auto language = cursiv::create_language<
    recipe::RecipeCoreServices, recipe::RecipeSharedCoreServices,
    recipe::install_core_module>();

const cursiv::ParseResult result = language.language.parser->parse(text);
if (!result || !result.diagnostics.empty())
  return fail("the sample does not parse cleanly");

const auto* model = cursiv::ast_cast<recipe::ast::Model>(result.value);
if (model == nullptr)
  return fail("the parse built no model");
```

`create_language` sets up your language services. Calling the parser directly
does not resolve references or run validation checks.

`result.cst().recovered()` reports syntax repair. `result.diagnostics` also
contains conversion errors; an AST can exist even when diagnostics are present.

`ast_cast<T>` checks the node's type and returns null if it does not match.

## Checking your validation

Add a separate test executable for validations:

```cmake title="tests/CMakeLists.txt"
add_executable(recipe_validation_test validation_test.cpp)
target_link_libraries(recipe_validation_test
    PRIVATE recipeCore cursiv::test cursiv::lsp)
add_test(NAME recipe_validation_test COMMAND recipe_validation_test)
```

Link `cursiv::lsp` when using the diagnostic expectation helpers.

```cpp title="tests/validation_test.cpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/ValidationHelper.hpp"
#include "cursiv/test/ExpectDiagnosticOptions.hpp"
#include "recipe/core/CoreServices.hpp"

namespace ct = cursiv::test;

void a_duplicate_use_is_refused() {
  const auto language = cursiv::create_language<
      recipe::RecipeCoreServices, recipe::RecipeSharedCoreServices,
      recipe::install_core_module>();

  auto result = ct::validationHelper(
      language.language,
      "unit gram\n"
      "ingredient Flour in gram\n"
      "recipe Twice {\n"
      "  use Flour 100\n"
      "  use Flour 200\n"
      "}\n");

  ct::expectError(result, "This ingredient is already used in this recipe.");
  result.dispose();
}

void a_clean_document_says_nothing() {
  const auto language = cursiv::create_language<
      recipe::RecipeCoreServices, recipe::RecipeSharedCoreServices,
      recipe::install_core_module>();

  auto result = ct::validationHelper(
      language.language,
      "unit gram\n"
      "ingredient Flour in gram\n"
      "recipe Once { use Flour 100 }\n");
  ct::expectNoIssues(result);
  result.dispose();
}

int main() {
  cursiv::test::count_expectations();
  a_duplicate_use_is_refused();
  a_clean_document_says_nothing();
  return cursiv::test::report("recipe_validation_test");
}
```

`validationHelper` builds a document and returns its diagnostics. Validation is
enabled by default in all document-building helpers, including `parseHelper`.
`expectError` and `expectWarning` accept a message and optional filters such as
an offset, diagnostic code or AST property.

Call `dispose()` to remove the document when finished, especially when reusing
services between tests. Cleanup is explicit. Here each test also creates a
separate workspace.

## Checking the editor's answers

The same module tests Language Server Protocol (LSP) editor features. Add
markers to the input text: `<|>` marks the cursor, and `<| |>` wraps the range
that the result must cover.

```cpp
#include "cursiv/test/ExpectedHover.hpp"

// `services` is a RecipeServices container with core and LSP modules installed.
(void)ct::expectHover(services,
                      {.text = "/** The gram unit. */\nunit gram\n"
                               "ingredient Flour in g<|>ram\n",
                       .hover = "The gram unit.",
                       .disposeAfterCheck = true});
```

Available expectations:

| Helper | Checks |
|---|---|
| `expectCompletion` | what may be written at the caret |
| `expectHover` | the documentation shown |
| `expectGoToDefinition` | where a name leads |
| `expectFindReferences` | every site that names a declaration |
| `expectRename` | the edit's reach, and the names refused |
| `expectHighlight` | the occurrences highlighted together |
| `expectSymbols` / `expectWorkspaceSymbols` | the outline, and the workspace search |
| `expectFoldings` | the ranges an editor may fold |
| `expectSemanticToken` | the colouring |
| `expectFormatting` | the layout on save |

Set `disposeAfterCheck = true` to remove the test document immediately after
the check. Otherwise, call `dispose()` on the returned result.

## Report test failures

Use `cursiv/test/Check.hpp` for assertions and a test exit code.

```cpp
CHECK(model->recipes.size() == 1);
return cursiv::test::report("recipe_grammar_test");
```

`report` prints the verdict and returns the exit code CTest reads. Call
`count_expectations()` first in `main` to collect all `expect*` failures before
reporting them.

## Further checks

The examples also include fuzz tests that generate documents and check their
trees, and recovery tests that compare repairs on broken input. See
[Test module](../reference/test.md#recovery-and-fuzz) when you need them.
