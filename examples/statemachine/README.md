# Statemachine

Use this example to learn named declarations, references and keyword
documentation. Files use the `.statemachine` extension.

```text
statemachine Door

events
    toggle

initialState Closed

state Closed
    toggle => Open
end

state Open
    toggle => Closed
end
```

A transition refers to a declared event and target state. Structural keywords
are case-insensitive; names retain their spelling.

## Build and try it

Run from the Cursiv repository root, with GCC and Ninja installed:

```sh
cmake --preset gcc-release
cmake --build --preset gcc-release --target statemachine_cli --parallel
./build/examples/statemachine/src/statemachine_cli \
  examples/statemachine/example/trafficlight.statemachine
```

Add `--dump` to inspect the syntax tree. `example/broken.statemachine` shows a
misspelled keyword and an incomplete state declaration.

## Read the implementation

- [Grammar](src/statemachine/core/StateMachineGrammar.hpp): `ikw` for keywords,
  `.doc` for hover text, and rules that assign names and references.
- [AST](src/statemachine/core/ast.hpp): states, events, commands and transitions.
- [Services](src/statemachine/core/): validation and reference resolution.

## Try it in VS Code

Follow the [shared editor setup](../README.md#try-an-editor-session) and select
**Run Statemachine Extension**. Try go-to-definition on a transition's target
and hover over `state` to see its keyword documentation.
