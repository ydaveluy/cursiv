# Examples

Choose an example for the feature you want to add. Each project includes a
grammar, AST, services, tests, CLI and VS Code extension.

| To learn… | Start with |
|---|---|
| Expressions and operator precedence | [Arithmetics](https://github.com/ydaveluy/cursiv/tree/main/examples/arithmetics) |
| Declarations, references and keyword documentation | [Statemachine](https://github.com/ydaveluy/cursiv/tree/main/examples/statemachine) |
| Packages, qualified names and formatting | [Domainmodel](https://github.com/ydaveluy/cursiv/tree/main/examples/domainmodel) |
| Several languages with cross-file references | [Requirements](https://github.com/ydaveluy/cursiv/tree/main/examples/requirements) |

## Run an example

From the Cursiv repository root, with GCC and Ninja installed:

```bash
cmake --preset gcc-release
cmake --build --preset gcc-release --target statemachine_cli --parallel
./build/examples/statemachine/src/statemachine_cli \
  examples/statemachine/example/doors.statemachine
```

The CLI reports `: ok` when the document has no error diagnostics. Use
`--dump` to inspect the syntax tree or `--root <folder>` to load declarations
from other files.

For VS Code setup and packaging, see the
[examples guide](https://github.com/ydaveluy/cursiv/tree/main/examples#try-an-editor-session).

## Adapt an example

Read `core/*Grammar.hpp` and `core/ast.hpp` first. They define the syntax and
the data it produces. Then read the validation, scoping or formatting service
that implements the feature you need.

To create your own project, use [Create a project](../learn/scaffold.md).
To extend another grammar, see [Derived grammars](../recipes/derived-grammars.md).
