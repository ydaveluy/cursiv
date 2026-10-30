# Examples

Each example includes a grammar, AST, services, tests, CLI and VS Code
extension. Choose one for the feature you want to add:

| To learn… | Start with |
|---|---|
| Expressions and operator precedence | [Arithmetics](https://github.com/ydaveluy/cursiv/tree/main/examples/arithmetics) |
| Declarations, references and keyword documentation | [Statemachine](https://github.com/ydaveluy/cursiv/tree/main/examples/statemachine) |
| Packages, qualified names and formatting | [Domainmodel](https://github.com/ydaveluy/cursiv/tree/main/examples/domainmodel) |
| Several languages with cross-file references | [Requirements](https://github.com/ydaveluy/cursiv/tree/main/examples/requirements) |

You can try these examples in your browser on the
[Try it online](../demo/index.md) page.

## More languages

[cursiv-grammar](https://github.com/ydaveluy/cursiv-grammar) holds more than
300 languages implemented with Cursiv, mostly adapted from
[ANTLR grammars-v4](https://github.com/antlr/grammars-v4). Each one builds on
its own, with a CLI, a language server and tests: a good starting point when
your language is close to an existing one.

## Run an example

From the Cursiv repository root, with GCC and Ninja installed:

```bash
cmake --preset gcc-release
cmake --build --preset gcc-release --target statemachine_cli --parallel
./build/examples/statemachine/src/statemachine_cli \
  examples/statemachine/example/doors.statemachine
```

The CLI reports `: ok` when the document has no errors. See
[Command line](../reference/cli.md) for its options, and the
[examples guide](https://github.com/ydaveluy/cursiv/tree/main/examples#try-an-editor-session)
to try an example in VS Code.

## Adapt an example

In `src/<example>/core/`, read `*Grammar.hpp` and `ast.hpp` first: they
define the syntax and the AST it builds. Then read the validation, scoping or
formatting service you need.

To start your own project, see [Create a project](../learn/scaffold.md). To
extend another grammar, see
[Reuse a grammar](../recipes/reuse-a-grammar.md#derive-a-dialect).
