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

`domainmodel_cli generate FILE` writes one Java class per entity, in the
folders its packages name; `-d DIR` chooses where. The generator only shows
how to turn a model into code: the validation does not check Java's reserved
words or name collisions, so a valid model can still give Java that does not
compile, for example a feature named `class`. Types are also written as the
model names them, without imports, and a datatype names no Java type, so even
`example/blog.dmodel` gives Java that does not compile: `Date` is not a Java
class, and `Snapshot` uses `Post` from another package.

## Read the implementation

- [Grammar](src/core/domainmodel/DomainModelGrammar.hpp): declarations and
  qualified names such as `package.Type`.
- [AST](src/core/domainmodel/ast.hpp): entities, features and packages.
- [Scoping](src/core/domainmodel/references/): symbols visible within and
  across packages.
- [Formatter](src/lsp/domainmodel/): spacing and indentation rules.

The [scoping recipe](../../docs/recipes/scoping.md) explains a small version of
the qualified-name implementation.

## Try it in VS Code

Follow the [shared editor setup](../README.md#try-an-editor-session) and select
**Run Domainmodel Extension**. Open `example/blog.dmodel` to try completion,
go-to-definition and formatting.
