# Qualified names and scoping

A scope is the set of declarations a reference can resolve to. By default, a
named node is visible inside its container, and top-level declarations are
visible from other documents. To reach a declaration nested in a package from
another document, export it under its qualified name:

```
package foo.bar {
  entity Thing { }
}

package other {
  entity Uses extends foo.bar.Thing { }
}
```

This recipe uses the AST of the [Domainmodel example](../examples/index.md).

## Export a qualified name

Override `collectExportedSymbols` to export each type under its full name.
Local scopes keep their default, so `Thing` still resolves inside its own
package:

```cpp title="core/references/QualifiedScopeComputation.hpp"
#pragma once

#include <string>
#include <vector>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/references/DefaultScopeComputation.hpp"
#include "cursiv/core/workspace/AstNodeDescriptionProvider.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "domainmodel/core/ast.hpp"

namespace dm {

class QualifiedScopeComputation final : public cursiv::DefaultScopeComputation {
 public:
  using DefaultScopeComputation::DefaultScopeComputation;

  std::vector<cursiv::AstNodeDescription> collectExportedSymbols(
      const cursiv::Document& document,
      const cursiv::CancellationToken& token = cursiv::kNoCancel) const override {
    std::vector<cursiv::AstNodeDescription> exported;
    for (cursiv::AstNode* node : document.parseResult.nodes) {
      cursiv::throw_if_cancelled(token);
      const auto* type = cursiv::ast_cast<const ast::Type>(node);
      if (type == nullptr || type->name.empty()) continue;

      std::string name(type->name);
      for (const cursiv::AstNode* parent = node->parent;
           parent != nullptr; parent = parent->parent) {
        if (const auto* package =
                cursiv::ast_cast<const ast::PackageDeclaration>(parent))
          name = std::string(package->name) + "." + name;
      }

      const auto stored = cursiv::copy_into(document.parseResult.cst().memory(), name);
      if (auto description = services.astNodeDescriptionProvider->createDescription(
              *node, stored, &document))
        exported.push_back(*description);
    }
    return exported;
  }
};

}  // namespace dm
```

A description borrows its name, so `copy_into` stores the computed name in
the document's memory. Do not resolve references here: linking starts only
after every document's scopes are ready.

## Install the implementation

In `install_core_module`, after the generated module, replace the default
service:

```cpp
services.scopeComputation =
    std::make_unique<dm::QualifiedScopeComputation>(services);
```

The default scope provider then finds `foo.bar.Thing` among the exported
names. Both documents must be built in the same workspace; the CLI does this
with `--root <dir>`.

## Check the result

With the test helpers from [Test your language](../learn/testing.md), mark the
declaration and the reference:

```cpp
(void)ct::expectGoToDefinition(*services,
    {.text = "package foo.bar { entity <|Thing|> { } }\n"
             "package other { entity U extends foo.bar.T<|>hing { } }\n",
     .disposeAfterCheck = true});
```

## Advanced: partially qualified names

The code above accepts full names everywhere and bare names in their own
package. To also accept partially qualified names in enclosing packages, the
Domainmodel example's `DomainModelScopeComputation` overrides
`collectLocalSymbols`. See [Services](../reference/services.md#document-lifecycle)
for the processing order, and [Case-insensitive languages](case-insensitive.md)
to compare names without case.
