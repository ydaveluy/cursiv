# Domainmodel

Use this example to learn packages, inheritance and qualified names. Files
use the `.dmodel` extension.

```text
datatype String

entity Person {
    name: String
}
```

Types such as `String` are references. The full sample also shows entities
that inherit from another entity and declarations inside packages.

## Build and try it

Run from the Cursiv repository root, with GCC and Ninja installed:

```sh
cmake --preset gcc-release
cmake --build --preset gcc-release --target domainmodel_cli --parallel
./build/examples/domainmodel/src/domainmodel_cli examples/domainmodel/example/blog.dmodel
```

Add `--dump` to inspect the syntax tree. `example/broken.dmodel` shows an
unresolved supertype.

## Read the implementation

- [Grammar](src/domainmodel/core/DomainModelGrammar.hpp): declarations and
  qualified names such as `package.Type`.
- [AST](src/domainmodel/core/ast.hpp): entities, features and packages.
- [Scoping](src/domainmodel/core/references/): symbols visible within and
  across packages.
- [Formatter](src/domainmodel/lsp/): spacing and indentation rules.

The [scoping recipe](../../docs/recipes/scoping.md) explains a small version of
the qualified-name implementation.

## Try it in VS Code

Follow the [shared editor setup](../README.md#try-an-editor-session) and select
**Run Domainmodel Extension**. Open `example/blog.dmodel` to try completion,
go-to-definition and formatting.
