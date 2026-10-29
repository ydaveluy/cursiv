# Build a language end to end

This tutorial builds a recipe language with declarations, nested blocks,
references and a validation check. With the
[required tools](index.md#before-you-start) installed, create a project named
`Recipe`:

```bash
curl -fsSLO https://raw.githubusercontent.com/ydaveluy/cursiv/main/docs/cursiv-new.cmake
cmake -DNAME=Recipe -P cursiv-new.cmake
cd recipe
```

Replace the hello-world sample in `example/hello.recipe` with this one. Do
not build yet: the steps below replace the hello-world sources first.

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

`Extends<AstNode>` provides the field types `string_view`, `vector` and
`reference`. `vector<T*>` holds child nodes. `reference<T>` holds a name that
resolves to a `T` after parsing. See [AST vocabulary](../reference/ast.md).

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

  struct Unit : rule<kw<"unit"> + assign<&ast::Unit::name>(ref<ID>)> {};

  struct Ingredient
      : rule<kw<"ingredient"> + assign<&ast::Ingredient::name>(ref<ID>)
             + kw<"in"> + assign<&ast::Ingredient::unit>(ref<ID>)> {};

  struct Use : rule<kw<"use"> + assign<&ast::Use::ingredient>(ref<ID>)
                    + assign<&ast::Use::amount>(ref<NUMBER>)> {};

  struct RecipeDecl
      : rule<kw<"recipe"> + assign<&ast::Recipe::name>(ref<ID>) + lit<"{">
             + many(append<&ast::Recipe::uses>(ref<Use>)) + lit<"}">> {};

  struct Model : rule<some(append<&ast::Model::units>(ref<Unit>)
                           | append<&ast::Model::ingredients>(ref<Ingredient>)
                           | append<&ast::Model::recipes>(ref<RecipeDecl>))> {};

  using entry = Model;
  using ignored = WS;
  using hidden = list<SL_COMMENT, ML_COMMENT>;
};

}  // namespace recipe
```

A `terminal` reads one token, such as a name or a number. A `rule` reads a
sequence of tokens and rules, and builds the AST class whose members it fills:
`Unit` builds an `ast::Unit` and `Model` an `ast::Model`. In a rule:

- `kw<"unit">` matches a whole keyword, and `lit<"{">` matches punctuation.
- `ref` calls a terminal or a rule.
- `assign` stores a value in a member, and `append` adds an element to a list.
- `some` reads one or more items, and `many` zero or more.

`entry` is the rule that reads a whole document. Whitespace and comments may
appear between any two tokens: `ignored` drops them, and `hidden` keeps them in
the syntax tree. The recipe rule is named `RecipeDecl` because C++ does not
allow a member to have the name of its class, `Recipe`.
[Write a grammar](write-a-grammar.md) explains each construct.

## 3. Replace the sample validator

The scaffold's validator checks hello-world types that no longer exist.
Replace both validator files with a check that rejects an ingredient used
twice in one recipe:

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

The type of the first parameter decides where a check runs: here, on every
`ast::Recipe` node. `.property<&ast::Use::ingredient>()` puts the error on the
ingredient name instead of the whole `use` line. `get()` returns the target of
a reference, or null when the name does not resolve; the linker already reports
that case. See [Custom validation](../recipes/validation.md).

## 4. Update the test and build

In `tests/grammar_test.cpp`, keep the setup. Replace the final size check,
which counts hello-world declarations, with:

```cpp
if (model.units.size() != 2 || model.ingredients.size() != 2 ||
    model.recipes.size() != 1)
  return fail("the model does not hold what the sample declares");
```

Now build and test:

```bash
cmake -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/recipe_cli example/hello.recipe
./build/src/recipe_cli --dump example/hello.recipe
```

The tests pass and the CLI reports `example/hello.recipe: ok`. `--dump`
prints the concrete syntax tree: the AST classes built and the tokens read.

## 5. Try the reference resolution

`in gram` refers to a `Unit`, and each `use` to an `Ingredient`. By default,
a named node is visible inside its container. Units and ingredients belong to
the `Model`, so the recipes see them.

In the sample, replace `use Flour 200` with `use Sugar 200` and run the CLI
again:

```bash
./build/src/recipe_cli example/hello.recipe
```

It reports an error containing:

```text
Could not resolve reference to Ingredient named 'Sugar'.
```

Restore `Flour`. Declarations may also come after their uses.

## 6. Try the validation

Add a second `use Flour 200` to the recipe and run the CLI again. The syntax
is valid, but your check reports:

```text
This ingredient is already used in this recipe.
```

Remove the duplicate. To turn these two cases into tests, see
[Test your language](testing.md).

## 7. Open the editor

Follow [Ship an editor extension](editor-integration.md) to launch the VS Code
client with **F5**. Completion offers keywords and reference targets,
go-to-definition and rename follow your references, and the duplicate check
shows as a diagnostic.

To add hover documentation, write `/** The gram unit. */` before `unit gram`.
Hover then shows it on `gram` and on each reference to it. Hover reads these
comments from the `hidden` terminal named `ML_COMMENT`, which stays in the
syntax tree. To highlight the Recipe keywords, update the extension's TextMate
grammar, as that guide explains.

## What to change next

| You want | Go to |
|---|---|
| Names qualified by a package | [Qualified names and scoping](../recipes/scoping.md) |
| Consistent spaces and indentation | [Formatting](../recipes/formatting.md) |
| Keywords in any case | [Case-insensitive languages](../recipes/case-insensitive.md) |
| A second language in the same project | [Several languages](../recipes/multiple-languages.md) |
| Use the AST in your program | [Read the AST](consume-the-ast.md) |
