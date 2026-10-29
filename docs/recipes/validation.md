# Custom validation

A validation check is a method registered for an AST type. This recipe uses
the Recipe language from the [tutorial](../learn/walkthrough.md).

## Write it

Declare the check in your validator class, here the tutorial's
`RecipeValidator`:

```cpp title="src/recipe/core/validation/RecipeValidator.hpp"
void checkNoDuplicateUse(const ast::Recipe& recipe,
                         cursiv::ValidationAcceptor& accept) const;
```

The type of the first parameter selects the nodes: this check runs on every
`Recipe` node. You do not walk the tree yourself.

```cpp title="src/recipe/core/validation/RecipeValidator.cpp"
#include "recipe/core/validation/RecipeValidator.hpp"

#include <unordered_set>

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

}  // namespace recipe
```

## Choose the diagnostic range

`accept.error(node, message)` underlines the whole node.
`.property<&ast::Use::ingredient>()` underlines only that member, which is
usually what the author needs:

```
  use Flour 200
      ^^^^^      with .property<>
  ^^^^^^^^^^^^^  without
```

`accept.warning`, `accept.information` and `accept.hint` work the same way.
Each also accepts a `TextRange` instead of a node.

## Register it

The scaffold's `register_validation_checks`, in the validator's `.cpp` file,
lists the checks. Add yours to the list:

```cpp title="src/recipe/core/validation/RecipeValidator.cpp"
services.validationRegistry->registerChecks(
    {cursiv::ValidationRegistry::makeValidationCheck<&V::checkNoDuplicateUse>(
        *services.validator)});
```

The validator must outlive the registry; the scaffold keeps it in
`services.validator`.

## What belongs here, and what does not

A check is for what the grammar accepts but the language forbids: a naming
convention, a duplicate, a constraint between two declarations. If a
construct is never legal, refuse it in the grammar instead: the parser then
reports a syntax error and recovers.

Reserved words are the exception. Keywords are contextual, so
`ingredient recipe in gram` parses with `recipe` as the name. To reserve a
word, keep the grammar permissive and report the name in a check:

```cpp
void RecipeValidator::checkNameNotReserved(
    const ast::Ingredient& ingredient,
    cursiv::ValidationAcceptor& accept) const {
  if (ingredient.name == "recipe" || ingredient.name == "use")
    accept.error(ingredient, "This name is a reserved word.")
        .property<&ast::Ingredient::name>();
}
```

Unresolved references need no check: the linker already reports them.

## Check the check

```cpp
auto result = ct::validationHelper(language.language,
    "unit gram\ningredient Flour in gram\n"
    "recipe Twice {\n  use Flour 100\n  use Flour 200\n}\n");

ct::expectError(result, "This ingredient is already used in this recipe.");
result.dispose();
```

Also check that a valid document passes, with `ct::expectNoIssues(result)`.
See [Test your language](../learn/testing.md) for the setup.
