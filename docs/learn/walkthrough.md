# Build a language end to end

This tutorial builds a recipe language with declarations, nested blocks,
references and a validation check. Use the prerequisites from
[Create a project](scaffold.md), then create a separate project named `Recipe`:

```bash
curl -fsSLO https://raw.githubusercontent.com/ydaveluy/cursiv/main/docs/cursiv-new.cmake
cmake -DNAME=Recipe -P cursiv-new.cmake
cd recipe
```

The scaffold starts with the hello-world language. Replace
`example/hello.recipe` with this sample, then update the source files below
before building:

```text
unit gram
unit ml

ingredient Flour in gram
ingredient Milk in ml

recipe Pancakes {
  use Flour 200
  use Milk 300
}
```

## 1. Define the AST

The abstract syntax tree (AST) holds the data your program will use. Replace
`core/ast.hpp` with:

```cpp title="src/recipe/core/ast.hpp"
#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace recipe::ast {

struct Unit : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Ingredient : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  reference<Unit> unit;
};

struct Use : cursiv::Extends<cursiv::AstNode> {
  reference<Ingredient> ingredient;
  string_view amount;
};

struct Recipe : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<Use*> uses;
};

struct Model : cursiv::Extends<cursiv::AstNode> {
  vector<Unit*> units;
  vector<Ingredient*> ingredients;
  vector<Recipe*> recipes;
};

}  // namespace recipe::ast
```

`Extends<AstNode>` provides the field aliases `string_view`, `vector` and
`reference`. A list of nodes uses `vector<T*>`. A `reference<T>` records a name
while parsing and resolves to a target later. See
[AST vocabulary](../reference/ast.md) for the complete field types.

## 2. Define the grammar

Replace the grammar header with:

```cpp title="src/recipe/core/RecipeGrammar.hpp"
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "recipe/core/ast.hpp"

namespace recipe {

struct Recipe : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct NUMBER : terminal<some(d)> {};

  struct UnitRule : rule<kw<"unit"> + assign<&ast::Unit::name>(ref<ID>)> {};

  struct IngredientRule
      : rule<kw<"ingredient"> + assign<&ast::Ingredient::name>(ref<ID>) +
             kw<"in"> + assign<&ast::Ingredient::unit>(ref<ID>)> {};

  struct UseRule
      : rule<kw<"use"> + assign<&ast::Use::ingredient>(ref<ID>) +
             assign<&ast::Use::amount>(ref<NUMBER>)> {};

  struct RecipeRule
      : rule<kw<"recipe"> + assign<&ast::Recipe::name>(ref<ID>) + lit<"{"> +
             many(append<&ast::Recipe::uses>(ref<UseRule>)) + lit<"}">> {};

  struct ModelRule
      : rule<some(append<&ast::Model::units>(ref<UnitRule>) |
                  append<&ast::Model::ingredients>(ref<IngredientRule>) |
                  append<&ast::Model::recipes>(ref<RecipeRule>))> {};

  using entry = ModelRule;
  using ignore = WS;
  using hidden = list<SL_COMMENT, ML_COMMENT>;
};

}  // namespace recipe
```

`kw<"unit">` matches a whole keyword; `lit<"{">` matches punctuation.
`assign` fills one member, `append` adds a list element, and `ref` calls a
terminal or rule. `some` requires at least one declaration; `many` allows none.

The members named by the directives determine the AST class. `UnitRule`
builds a `Unit`, and the `Model::units`, `ingredients` and `recipes` appends
make `ModelRule` build a `Model`. You may write `create<ast::Model>` explicitly,
but it is unnecessary here. Replacing `some` with `many` would also accept
an empty document and build an empty `Model`.

## 3. Replace the sample validator

The scaffold's Hello World validator refers to `Person`, `Model::persons`
and `Model::greetings`. Replace both validator files so their checks use the
Recipe AST defined above. Do this before building. The following check rejects
using the same ingredient twice in one recipe:

```cpp title="src/recipe/core/validation/RecipeValidator.hpp"
#pragma once

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "recipe/core/ast.hpp"

namespace recipe {

class RecipeValidator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkNoDuplicateUse(const ast::Recipe& recipe,
                           cursiv::ValidationAcceptor& accept) const;
};

struct RecipeCoreServices;
void register_validation_checks(RecipeCoreServices& services);

}  // namespace recipe
```

```cpp title="src/recipe/core/validation/RecipeValidator.cpp"
#include "recipe/core/validation/RecipeValidator.hpp"

#include <memory>
#include <unordered_set>

#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "recipe/core/CoreServices.hpp"

namespace recipe {

void RecipeValidator::checkNoDuplicateUse(
    const ast::Recipe& recipe, cursiv::ValidationAcceptor& accept) const {
  std::unordered_set<const ast::Ingredient*> seen;
  for (const ast::Use* const use : recipe.uses) {
    if (use == nullptr) continue;
    const ast::Ingredient* const target = use->ingredient.get();
    if (target != nullptr && !seen.insert(target).second)
      accept.error(*use, "This ingredient is already used in this recipe.")
          .property<&ast::Use::ingredient>();
  }
}

void register_validation_checks(RecipeCoreServices& services) {
  using V = RecipeValidator;
  services.validator = std::make_unique<const V>(services);
  services.validationRegistry->registerChecks(
      {cursiv::ValidationRegistry::makeValidationCheck<&V::checkNoDuplicateUse>(
          *services.validator)});
}

}  // namespace recipe
```

The registry runs this method on each `ast::Recipe` node.
`.property<&ast::Use::ingredient>()` puts the diagnostic under the ingredient
name. An unresolved reference returns null from `get()`; the linker reports
it, so this check skips it. See [Custom validation](../recipes/validation.md).

## 4. Update the test and build

The scaffold's test checks the `persons` and `greetings` lists of its Hello
World sample. In `tests/grammar_test.cpp`, keep the setup and replace that
final size check with:

```cpp
if (model.units.size() != 2 || model.ingredients.size() != 2 ||
    model.recipes.size() != 1)
  return fail("the model does not hold what the sample declares");
```

Now configure, build and test:

```bash
cmake -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/recipe_cli example/hello.recipe
./build/src/recipe_cli --dump example/hello.recipe
```

The tests pass and the CLI reports `example/hello.recipe: ok`. `--dump`
prints the concrete syntax tree, showing the rules and tokens that matched.

## 5. Try the reference resolution

`ingredient Flour in gram` refers to the `Unit` named `gram`. Each `use`
refers to an `Ingredient`. The default scoping services expose named children
inside their container; here the units and ingredients belong to the `Model`,
so they are visible from nested recipes too.

In the sample's `recipe Pancakes` block, replace `use Flour 200` with
`use Sugar 200`, then run the CLI again:

```bash
./build/src/recipe_cli example/hello.recipe
```

It reports an error containing:

```text
Could not resolve reference to Ingredient named 'Sugar'.
```

Restore `Flour`. Forward references work as well: declarations do not have to
appear before their uses.

## 6. Try the validation

Add a second `use Flour 200` inside the recipe and run the CLI again. The
syntax is valid, but your check reports:

```text
This ingredient is already used in this recipe.
```

Remove the duplicate before running the tests again. To make these two cases
permanent tests, continue with [Test your language](testing.md).

## 7. Open the editor

Follow [Ship an editor extension](editor-integration.md) to launch the VS Code
client with **F5**. Completion offers allowed keywords and reference targets;
go-to-definition and rename use the references you declared. The duplicate
ingredient check appears as an editor diagnostic too.

To try hover documentation, add `/** The gram unit. */` before `unit gram`.
The `ML_COMMENT` terminal keeps the comment in the CST so the documentation
provider can find it. Update the extension's TextMate grammar when you want
its initial syntax highlighting to recognize the Recipe keywords.

## What to change next

| You want | Go to |
|---|---|
| Names qualified by a package | [Qualified names and scoping](../recipes/scoping.md) |
| Consistent spaces and indentation | [Formatting](../recipes/formatting.md) |
| Keywords in any case | [Case-insensitive languages](../recipes/case-insensitive.md) |
| A second language in the same project | [Several languages](../recipes/multiple-languages.md) |
| Use the AST in your program | [Read the AST](consume-the-ast.md) |
