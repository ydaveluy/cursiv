# Write a grammar

In this guide, you write the grammar of a small calculator language step by
step, then check the tree it builds. The
[grammar reference](../reference/grammar.md) gives the exact rules. Programs in
the finished language look like this:

```text title="example/hello.calc"
// A definition names an expression; an evaluation computes one.
def pi: 3.14159;
def area(r): pi * r ^ 2;
area(2);
(1 + 2) * 3;
```

## 1. Set up the project

Create a project named `Calc`, as in [Create a project](scaffold.md):

```bash
curl -fsSLO https://raw.githubusercontent.com/ydaveluy/cursiv/main/docs/cursiv-new.cmake
cmake -DNAME=Calc -P cursiv-new.cmake
cd calc
```

Replace `example/hello.calc` with the program above. Then replace
`src/calc/core/ast.hpp` with the AST classes that the grammar fills:

```cpp title="src/calc/core/ast.hpp"
#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace calc::ast {

struct Statement : cursiv::Extends<cursiv::AstNode> {};
struct Expression : cursiv::Extends<cursiv::AstNode> {};

struct Model : cursiv::Extends<cursiv::AstNode> {
  vector<Statement*> statements;
};

struct Evaluation : cursiv::Extends<Statement> {
  Expression* expression;
};

struct Parameter : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Definition : cursiv::Extends<Statement> {
  string_view name;
  vector<Parameter*> params;
  Expression* body;
};

struct Number : cursiv::Extends<Expression> {
  float64 value;
};

struct Call : cursiv::Extends<Expression> {
  string_view name;
  vector<Expression*> args;
};

struct Binary : cursiv::Extends<Expression> {
  Expression* left;
  string_view op;
  Expression* right;
};

}  // namespace calc::ast
```

`Statement` and `Expression` are base classes: `statements` holds definitions
and evaluations, and an `Expression*` member holds any expression. See
[AST vocabulary](../reference/ast.md).

The scaffold's checks use hello-world classes that Calc does not have. Replace
the two files in `src/calc/core/validation/` with a validator that checks
nothing yet. [Custom validation](../recipes/validation.md) shows how to add
checks.

```cpp title="src/calc/core/validation/CalcValidator.hpp"
#pragma once

#include "cursiv/core/services/DefaultCoreService.hpp"

namespace calc {

class CalcValidator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;
};

struct CalcCoreServices;
void register_validation_checks(CalcCoreServices& services);

}  // namespace calc
```

```cpp title="src/calc/core/validation/CalcValidator.cpp"
#include "calc/core/validation/CalcValidator.hpp"

#include <memory>

#include "calc/core/CoreServices.hpp"

namespace calc {

void register_validation_checks(CalcCoreServices& services) {
  services.validator = std::make_unique<const CalcValidator>(services);
}

}  // namespace calc
```

## 2. Read the tokens

In `src/calc/core/CalcGrammar.hpp`, delete the members of `struct Calc`. Each
step below appends members to it. Start with the terminals and the trivia:

```cpp
struct WS : terminal<some(s)> {};
struct COMMENT : terminal<through(lit<"//">, &eol | eof)> {};
struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
struct NUMBER : terminal<as<double>(some(d) + option(lit<"."> + some(d)))> {};

using ignored = WS;
using hidden = COMMENT;
```

A **terminal** reads one token, with no trivia inside. `s`, `w` and `d` match
one whitespace, word or digit character, and `cr<"a-zA-Z_">` one character
from a range. `some(e)` reads `e` once or more, `many(e)` zero or more times
and `option(e)` at most once. `COMMENT` runs from `//` to the end of the line.
`as<double>` converts the text of `NUMBER` to a `double`.

Whitespace and comments are **trivia**: they may appear between any two
tokens of a rule. `ignored` drops them from the syntax tree; `hidden` keeps
them, so a comment can document what follows.

Keywords and punctuation need no terminal: rules write them in place, as
`kw<"def">` and `lit<";">`. `kw<"def">` does not match the start of `define`.
See [Text](../reference/grammar.md#text).

## 3. Write the first rules

A **rule** reads a sequence of tokens and rules, with trivia allowed between
them:

```cpp
struct Statement;
struct Evaluation;
struct Definition;
struct Expression;

struct Model : rule<many(append<&ast::Model::statements>(ref<Statement>))> {};

struct Statement : rule<ref<Definition> | ref<Evaluation>> {};

struct Evaluation
    : rule<assign<&ast::Evaluation::expression>(ref<Expression>)
           + lit<";">> {};

using entry = Model;
```

| Spelling | Meaning |
|---|---|
| `ref<R>` | Call the terminal or rule `R`. |
| `a + b` | Read `a`, then `b`. |
| `a | b` | Read `a` or `b`. |
| `assign<&T::m>(e)` | Store the value of `e` in the member `m`. |
| `append<&T::m>(e)` | Add the value of `e` to the list `m`. |

A rule builds the class whose members it writes: `Model` builds an
`ast::Model` and `Evaluation` an `ast::Evaluation`. `Statement` writes nothing,
so it returns the node of the rule it calls. Put `append` inside the
repetition, as in `Model`, so that each turn adds one statement.

A rule can only name rules declared above it. The four declarations at the top
let rules name rules defined further down. `entry` is the rule that reads a
whole document.

## 4. Optional parts and lists

A definition may take parameters, between parentheses and separated by commas:

```cpp
struct Parameter : rule<assign<&ast::Parameter::name>(ref<ID>)> {};

struct Definition
    : rule<kw<"def"> + assign<&ast::Definition::name>(ref<ID>)
           + option(lit<"(">
                    + many(append<&ast::Definition::params>(ref<Parameter>),
                           lit<",">)
                    + lit<")">)
           + lit<":"> + assign<&ast::Definition::body>(ref<Expression>)
           + lit<";">> {};
```

`option` makes the parameter list optional: `def pi: 3.14159;` has none.
With a second argument, `many` and `some` read a separator between items:
`many(e, lit<",">)` accepts nothing, `a` or `a, b`. See
[Repetition](../reference/grammar.md#repetition).

## 5. Recursion

Expressions contain expressions: `(1 + 2) * 3` holds `1 + 2`, and `area(2)`
holds its argument. `Primary` reads one operand and calls `Expression` for the
inner ones:

```cpp
struct Primary
    : rule<(lit<"("> + ref<Expression> + lit<")">)
           | assign<&ast::Number::value>(ref<NUMBER>)
           | (assign<&ast::Call::name>(ref<ID>)
              + option(lit<"(">
                       + option(some(append<&ast::Call::args>(ref<Expression>),
                                     lit<",">)
                                + option(lit<",">))
                       + lit<")">))> {};
```

Each alternative returns a different node:

- A parenthesized expression writes nothing, so it returns the inner
  expression: parentheses add no node to the AST.
- A number builds a `Number`.
- A name builds a `Call`, with arguments when parentheses follow. A name
  alone, such as `pi`, is a call with no arguments. The argument list accepts
  a trailing comma.

`Expression` comes in the next step; the declaration from step 3 lets
`Primary` call it already. Put each alternative that is a sequence in
parentheses, as above; see [C++ spelling](../reference/grammar.md#composition).

## 6. Operators and precedence

`infix` reads operands separated by binary operators and groups them by
precedence:

```cpp
struct Expression
    : infix<Primary, ast::Binary,
            levels(right(lit<"^">),
                   left(lit<"*"> | lit<"/">),
                   left(lit<"+"> | lit<"-">))> {};
```

`levels` lists the levels from the tightest to the loosest. The operators of
one level share its precedence:

| Input | Reads as |
|---|---|
| `1 + 2 * 3` | `1 + (2 * 3)` |
| `8 / 4 / 2` | `(8 / 4) / 2`: `left` groups to the left |
| `2 ^ 3 ^ 2` | `2 ^ (3 ^ 2)`: `right` groups to the right |

Each operator builds a `Binary`, whose `op` holds the operator's text, such
as `*`. A lone operand stays as it is: `area(2)` is a `Call`, not a `Binary`.
See [Operator precedence](../reference/grammar.md#operator-precedence) for
more options.

## 7. Check the result

Build the project and run its command line on the sample:

```bash
cmake -B build
cmake --build build --parallel
./build/src/calc_cli example/hello.calc
```

If the parser generator refuses the grammar, the build stops. Look up the
reported code or message in [Grammar errors](../reference/refusals.md).

The command line prints `example/hello.calc: ok`, or the line and column of a
syntax error. Add `--dump` to print the concrete syntax tree (CST), one node
per line with its source range. Unlike the AST, the CST keeps the parentheses.

Then check the tree in a test. In `tests/grammar_test.cpp`, keep the setup.
Replace the final size check with:

```cpp
if (model.statements.size() != 4)
  return fail("the model does not hold what the sample declares");

// `1 + 2 * 3` must group as `1 + (2 * 3)`.
const cursiv::ParseResult sum =
    language.language.parser->parse("1 + 2 * 3;");
const auto* tree = cursiv::ast_cast<calc::ast::Model>(sum.value);
if (tree == nullptr || tree->statements.size() != 1)
  return fail("1 + 2 * 3 is not one statement");
const auto* evaluation =
    cursiv::ast_cast<calc::ast::Evaluation>(tree->statements[0]);
const auto* plus =
    evaluation == nullptr
        ? nullptr
        : cursiv::ast_cast<calc::ast::Binary>(evaluation->expression);
if (plus == nullptr || plus->op != "+" ||
    cursiv::ast_cast<calc::ast::Binary>(plus->right) == nullptr)
  return fail("1 + 2 * 3 is not read as 1 + (2 * 3)");
```

`ast_cast<T>` returns null when a node is not a `T`, so the test checks each
class before reading its members. Run the tests with
`ctest --test-dir build --output-on-failure`.

## Next

| You want | Go to |
|---|---|
| Names that may also be keywords | [Literals and keywords](../reference/grammar.md#literals-and-keywords) |
| To know which parse wins when several match | [Choosing between parses](../reference/grammar.md#choosing-between-parses) |
| Calls linked to their definitions, and checks | [Build a language end to end](walkthrough.md) |
| Tests for validation and editor features | [Test your language](testing.md) |
| A fuller calculator, with references and an interpreter | [Arithmetics example](https://github.com/ydaveluy/cursiv/tree/main/examples/arithmetics) |
