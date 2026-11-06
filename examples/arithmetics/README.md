# Arithmetics

Use this example to learn expression parsing, operator precedence and
references to named definitions. Files use the `.calc` extension.

```text
module demo

def pi: 3.14159;
def area(r): pi * r ^ 2;

area(2 + 3) * 2;
```

## Build and try it

Run from the Cursiv repository root, with GCC and Ninja installed:

```sh
cmake --preset gcc-release
cmake --build --preset gcc-release --target arithmetics_cli --parallel
./build/examples/arithmetics/src/arithmetics_cli examples/arithmetics/example/demo.calc
```

The CLI checks syntax, references and validation rules. Add `--dump` to inspect
the parsed syntax tree. Run `arithmetics_cli eval <file>` to print the value of
each evaluation; one that has no value, such as a division by zero, is reported
on stderr and the command exits with `1`.

## Read the implementation

- [Grammar](src/arithmetics/ArithmeticsGrammar.hpp): `infix` defines
  operator precedence and associativity.
- [AST](src/arithmetics/ast.hpp): definitions, calls and expressions.
- [Services](src/arithmetics/): validation and expression evaluation.

`example/demo.calc` is valid input. `example/broken.calc` shows a syntax error
caused by a missing semicolon.

## Try it in VS Code

Follow the [shared editor setup](../README.md#try-an-editor-session) and select
**Run Arithmetics Extension**. The editor provides completion, navigation and
diagnostics for `.calc` files.
