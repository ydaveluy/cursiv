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

This recipe uses the AST and `QualifiedNameProvider` of the
[Domainmodel example](../examples/index.md).

## Export a qualified name

Override `collectExportedSymbols` to export each type under its full name.
The bare name `Thing` inside its package comes from the local scope.

```cpp title="DomainModelScopeComputation.cpp"
std::vector<cursiv::AstNodeDescription>
DomainModelScopeComputation::collectExportedSymbols(
    const cursiv::Document& document, const cursiv::CancellationToken& token) const {
  std::vector<cursiv::AstNodeDescription> exported;
  for (cursiv::AstNode* node : document.parseResult.nodes()) {
    cursiv::throwIfCancelled(token);
    const auto* type = cursiv::ast_cast<ast::Type>(node);
    if (type == nullptr || type->name.empty()) continue;

    std::string_view name = type->name;
    if (const auto* package = cursiv::ast_cast<ast::PackageDeclaration>(node->parent))
      name = _dm.qualifiedNameProvider->keepQualifiedName(document, *package, name);
    if (auto symbol = _dm.astNodeDescriptionProvider->createDescription(*node, name, &document))
      exported.push_back(*symbol);
  }
  return exported;
}
```

Here `_dm` is the language's `DomainModelCoreServices`. A top-level type can
use its AST name directly. For a nested type, `keepQualifiedName` collects
the enclosing package names and uses `document.keepText` to keep the result
alive until the next parse. The index borrows that text. Compute exports from
this document's declarations, since other files may still be building.

## Install the implementation

In `install_core_module`, install the name provider before the scope computation:

```cpp
services.qualifiedNameProvider =
    std::make_unique<dm::QualifiedNameProvider>(services.shared);
services.scopeComputation =
    std::make_unique<dm::DomainModelScopeComputation>(services);
```

The default scope provider then finds `foo.bar.Thing` among the exported
names. Both documents must be built in the same workspace; the CLI does this
with `--root <dir>`.

## Test it

With the test helpers from [Test your language](../learn/testing.md), mark the
declaration and the reference:

```cpp
(void)ct::expectGoToDefinition(services,
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
