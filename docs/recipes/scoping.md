# Qualified names and scoping

A scope is the set of declarations a reference can resolve to. By default,
named nodes are visible inside their container, and top-level declarations
are exported to other documents. To reference a declaration inside a package
from another document, export its qualified name:

```
package foo.bar {
  entity Thing { }
}

package other {
  entity Uses extends foo.bar.Thing { }
}
```

This recipe uses the AST from the
[Domainmodel example](../examples/index.md).

## Export a qualified name

Override `collectExportedSymbols` to visit nested types and export them under
their full names. This small implementation keeps the default local scopes,
so `Thing` still resolves inside its own package:

`core/references/QualifiedScopeComputation.hpp`:

```cpp
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

Descriptions borrow their names. `copy_into` keeps a computed name in the
document's parse memory, rather than leaving a view into a temporary string.
Do not resolve references during export or local-scope computation; linking
starts after all documents have their scopes prepared.

## Install the implementation

After installing the generated module in `install_core_module`, replace the
default service:

```cpp
services.scopeComputation =
    std::make_unique<dm::QualifiedScopeComputation>(services);
```

The default `ScopeProvider` searches local containers first, then the global
index, filtering candidates by the reference's target types. It can therefore
find the exported `foo.bar.Thing` without a custom provider.

For cross-file references, build both documents in the same workspace. The CLI
does this when passed `--root <dir>`.

## Check the result

With your LSP services and the test helpers from
[Test your language](../learn/testing.md), mark both the declaration and the
reference:

```cpp
(void)ct::expectGoToDefinition(*services,
    {.text = "package foo.bar { entity <|Thing|> { } }\n"
             "package other { entity U extends foo.bar.T<|>hing { } }\n",
     .disposeAfterCheck = true});
```

## Advanced: partially qualified names

The implementation above supports full names globally and bare names in
their local container. The Domainmodel example's
`DomainModelScopeComputation` also overrides `collectLocalSymbols` to support
partially qualified names in enclosing packages. See
[Services](../reference/services.md#document-lifecycle) for the processing
order, and [Case-insensitive languages](case-insensitive.md) to change name
comparison.
