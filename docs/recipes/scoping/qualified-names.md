# Qualified Names

Let users reference declarations through dotted names such as `billing.Invoice` or `company.sales.Customer`.

Pegium's `domainmodel` example uses this pattern for packages and types: globally exported names can be qualified, while local lookup still keeps short names available inside the right container.

## What is the problem?

Qualified names matter once one short name is no longer enough. When users can write `blog.User` or `sales.Invoice`, the language can disambiguate declarations without flattening everything into one namespace.

Users still expect local short names to work inside the right container. So qualified names are rarely just a lookup problem. They are a question of how symbols are exported and how local symbols are precomputed.

## What needs to change?

You don't replace the linker. You customize the exported names and the local symbols that the default scope provider later consumes.

The `domainmodel` example does this with:

- a `QualifiedNameProvider`
- a custom `DomainModelScopeComputation`
- the existing default scope provider

This is cheaper than a heavily custom scope lookup: most work happens once per document during scope computation, not once per reference during completion or linking.

## Step 1: define how names are joined

`examples/domainmodel/src/domainmodel/core/references/QualifiedNameProvider.cpp` keeps the rule small and explicit:

```cpp
std::string QualifiedNameProvider::getQualifiedName(std::string_view qualifier,
                                                    std::string_view name) const {
  if (qualifier.empty()) {
    return std::string(name);
  }
  return std::string(qualifier) + "." + std::string(name);
}
```

A second overload walks up the package chain so a nested type gets its full dotted name in one call:

```cpp
std::string QualifiedNameProvider::getQualifiedName(
    const ast::PackageDeclaration &qualifier, std::string_view name) const;
```

## Step 2: export global symbols under qualified names

`QualifiedNameProvider` lives directly on the language's typed container, so the scope computation — invoked by the framework through a base reference — needs a typed way to reach it. `DomainModelScopeComputation` derives only `pegium::references::DefaultScopeComputation` and takes the language's own `const DomainModelCoreServices &` in its constructor. It stores that reference in a private `services` member that shadows the base back-reference (`DefaultScopeComputation::services`, typed as the framework's core services) with the typed container, so the service is reachable directly as `services.qualifiedNameProvider`, with no `dynamic_cast` and no null branch (see [Adding your own services](../../reference/configuration-services.md#adding-your-own-services)):

```cpp
class DomainModelScopeComputation final
    : public pegium::references::DefaultScopeComputation {
public:
  explicit DomainModelScopeComputation(const DomainModelCoreServices &services)
      : pegium::references::DefaultScopeComputation(services),
        services(services) {}
  // ...
private:
  const DomainModelCoreServices &services;
};
```

`DomainModelScopeComputation::collectExportedSymbols(...)` then walks every type in the document and prefixes the local name with the enclosing package chain before publishing it:

```cpp
const auto *qualifiedNameProvider = services.qualifiedNameProvider.get();
// ... for each exported type, with `name` from nameProvider->getName(*type):
if (const auto *package =
        pegium::ast_ptr_cast<const PackageDeclaration>(type->getContainer());
    package != nullptr && qualifiedNameProvider != nullptr) {
  *name = qualifiedNameProvider->getQualifiedName(*package, *name);
}
```

The exported name becomes something like `foo.bar.Customer`.

## Step 3: keep local names usable inside containers

Exporting qualified names isn't enough. Inside a package, users still expect short names to work.

The example overrides `collectLocalSymbols(...)` and builds local descriptions per container. Nested descriptions are re-added with a qualified form where needed, then stored in `document.localSymbols` — the data structure the default scope provider consumes later.

## Why the default scope provider still works

`pegium::references::DefaultScopeProvider` already combines:

- local symbols from the current container chain
- exported symbols from the workspace index

Once your scope computation emits the right names, the default lookup logic remains sufficient. That's why qualified-name support is a scope-computation problem rather than a linker problem.

## When to go further

If your language also needs imports, visibility modifiers, or access rules that depend on the reference site, continue with [Custom Scope Provider](../custom-scope-provider.md).

## Related pages

- [Examples: DomainModel](../../examples/domainmodel.md)
- [Custom Scope Provider](../custom-scope-provider.md)
- [Workspace Lifecycle](../../build-a-language/workspace.md)
