# Custom validation

A validation check is a method registered for an AST type. This example uses
the Recipe language from the [tutorial](../learn/walkthrough.md).

## Write it

`src/recipe/core/validation/RecipeValidator.hpp`:

```cpp
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

}  // namespace recipe
```

The first parameter is what keys the check: this one runs on every `Recipe`
node and on no other. You do not walk the tree — the framework visits, and
calls what matches.

`src/recipe/core/validation/RecipeValidator.cpp`:

```cpp
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

`accept.error(node, message)` underlines the **node's** span — the whole `use`
line here. `.property<&ast::Use::ingredient>()` moves it to where that member
was written, which is almost always what an author wants:

```
  use Flour 200
      ^^^^^      with .property<>
  ^^^^^^^^^^^^^  without
```

`accept.warning(...)`, `accept.information(...)` and `accept.hint(...)` are the
other severities, and all four take the same continuation. Each also has an
overload taking a `TextRange` instead of a node, for a span you computed.

## Register it

In `CoreModule.cpp`, include `RecipeValidator.hpp` and add this function inside
`namespace recipe`. Call it after installing the generated module:

`src/recipe/core/CoreModule.cpp`:

```cpp
void register_validation_checks(RecipeCoreServices& services) {
  using V = RecipeValidator;
  services.validator = std::make_unique<const V>(services);
  const V& validator = *services.validator;
  services.validationRegistry->registerChecks(
      {cursiv::ValidationRegistry::makeValidationCheck<&V::checkNoDuplicateUse>(
           validator)});
}
```

The validator passed to `makeValidationCheck` must outlive the registry.

## What belongs here, and what does not

A check is for **what the grammar accepts but the language does not**: a naming
convention, a duplicate, a fact no single node can see, a constraint between
two declarations.

It is not for what the grammar can refuse on its own. If a construct is never
legal, spell that in the grammar and get a syntax error with recovery, rather
than a clean parse followed by a diagnostic.

Nor is it for unresolved references — the linker already reports those:

```
a.recipe:9:7: error: Could not resolve reference to Ingredient named 'Sugar'. [recipe:linking-error]
```

## Check the check

```cpp
auto result = ct::validationHelper(language.language,
    "unit gram\ningredient Flour in gram\n"
    "recipe Twice {\n  use Flour 100\n  use Flour 200\n}\n");

ct::expectError(result, "This ingredient is already used in this recipe.");
result.dispose();
```

Also check a valid document with `ct::expectNoIssues(result)`. See
[Test your language](../learn/testing.md) for the setup and both cases.
