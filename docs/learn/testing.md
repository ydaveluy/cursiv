# Test your language

The scaffold's test parses the sample and checks its abstract syntax tree
(AST). As your language grows, test that:

1. A valid sample parses and produces the expected AST.
2. Each validation reports an invalid case and accepts a valid one.
3. Cross references resolve, and missing targets produce diagnostics.
4. Custom editor providers return the expected results.

The examples use the Recipe language from the [tutorial](walkthrough.md). Run
the tests with:

```bash
ctest --test-dir build --output-on-failure
```

## Check the parse result

In `tests/grammar_test.cpp`, keep the setup and update the AST checks when the
language changes. Check the parse result and its diagnostics before reading
the tree:

```cpp title="tests/grammar_test.cpp"
const auto language = cursiv::create_language<
    recipe::RecipeCoreServices, recipe::RecipeSharedCoreServices,
    recipe::install_core_module>();

const cursiv::ParseResult result = language.language.parser->parse(text);
if (!result || !result.diagnostics.empty())
  return fail("the sample does not parse cleanly");

const auto* const root = cursiv::ast_cast<recipe::ast::Model>(result.value);
if (root == nullptr)
  return fail("the parse built no model");
const auto& model = *root;
```

`create_language` sets up your language services. The parser alone does not
resolve references or run validation. `ast_cast<T>` returns null when the node
is not a `T`.

## Checking your validation

Add a test executable for validation. It links `cursiv::lsp` for the
diagnostic helpers:

```cmake title="tests/CMakeLists.txt"
add_executable(recipe_validation_test validation_test.cpp)
target_link_libraries(recipe_validation_test
    PRIVATE recipeCore cursiv::test cursiv::lsp)
add_test(NAME recipe_validation_test COMMAND recipe_validation_test)
```

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

`validationHelper` builds a document and returns its diagnostics.
`expectError` and `expectWarning` take a message and optional filters, such as
an offset, a diagnostic code or an AST property. `dispose()` removes the
document when you are done.

`count_expectations()` collects the failures of the `expect*` helpers, and
`report` prints the verdict and returns the exit code that CTest reads. For
your own assertions, use `CHECK(condition)` from `cursiv/test/Check.hpp`.

## Checking the editor's answers

The same module tests editor features. In the input text, `<|>` marks the
cursor and `<| |>` wraps the range the result must cover:

```cpp
#include "cursiv/test/ExpectedHover.hpp"

// `services` is a RecipeServices container with core and LSP modules installed.
(void)ct::expectHover(services,
                      {.text = "/** The gram unit. */\nunit gram\n"
                               "ingredient Flour in g<|>ram\n",
                       .hover = "The gram unit.",
                       .disposeAfterCheck = true});
```

`disposeAfterCheck = true` removes the test document after the check;
otherwise, call `dispose()` on the result. Completion, go-to-definition,
references, highlights, rename, symbols, folding, semantic tokens and
formatting have similar helpers, listed in
[Test module](../reference/test.md#check-editor-features).

## Further checks

The examples also include fuzz tests, which generate documents and check
their trees, and recovery tests, which compare repairs of broken input. See
[Test module](../reference/test.md#recovery-and-fuzz).
