# Requirements and Tests

Use this example to learn how two languages share a workspace and refer to
each other's declarations:

- `.req` files declare requirements and environments.
- `.tst` files declare tests and the requirements they cover.

For example, a requirement can be declared as:

```text
req R001 "A user can sign in"
```

A test in another file can refer to it:

```text
tst T001_sign_in tests R001
```

## Build and try it

Run from the Cursiv repository root, with GCC and Ninja installed:

```sh
cmake --preset gcc-release
cmake --build --preset gcc-release --target requirements_cli --parallel
./build/examples/requirements/src/requirements_cli \
  --root examples/requirements/example \
  examples/requirements/example/tests.tst
```

`--root` loads both languages' files into one workspace so requirement names
resolve. Without it, the CLI loads only the named file and reports references
to declarations in other files as unresolved.

## Read the implementation

- [Grammar](src/requirements/core/RequirementsGrammar.hpp): a common set of
  rules with a separate entry rule for each language.
- [AST](src/requirements/core/ast.hpp): requirements, environments and tests.
- [Services](src/requirements/core/): registration of both languages and
  cross-document validation.

See [Several languages in one project](../../docs/recipes/multiple-languages.md)
for the setup steps.

## Try it in VS Code

Follow the [shared editor setup](../README.md#try-an-editor-session) and select
**Run Requirements and Tests Extension**. Open the example folder, then use
go-to-definition from a requirement name in `tests.tst`.

Both languages run in one server, with ids `requirements-lang` and `tests-lang`.
Opening their containing folder makes the referenced files available without
opening every file individually.
