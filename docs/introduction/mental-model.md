# Mental model

These four concepts help you write a grammar, read its results and customize
language features.

## 1. Your headers define the language

You write the abstract syntax tree (AST) structs and the grammar rules that
fill their members.
Build with CMake to generate the parser and language module.

```
ast.hpp + MyLangGrammar.hpp → CMake build → parser and language module
```

Never edit the generated tree. Change the declaration and build again.

## 2. Two trees, and they answer different questions

The **CST** (concrete syntax tree) records the parsed syntax and its source
positions, including comments declared as `hidden`. Editor services use it
to find text ranges and the token under the cursor.

The **AST** (abstract syntax tree) contains your structs and their values:
names, child nodes, lists and references. Your validation, interpreter or
code generator usually works with this tree.

## 3. A reference is read, then resolved

A reference connects a written name to a declaration. For example,
`reference<Person>` records a person's name while parsing. The **linker** then
finds the declaration among the names visible at that position, called its
**scope**. Forward and cross-file references are supported.

```cpp
struct Greeting : cursiv::Extends<cursiv::AstNode> {
  reference<Person> person;   // written as a name, resolved later
};
```

The default scope makes every named node visible inside its container. A
language with packages customizes scoping — see
[Qualified names and scoping](../recipes/scoping.md).

## 4. Services live in two containers

A service implements a feature, such as parsing, validation or completion.
The scaffold groups services in two containers:

- **Shared services** manage the workspace's documents and exported names.
- **Language services** provide one language's parser, references, validation
  and editor features.

A workspace can hold several languages. To customize a feature, replace or
add a service in your language's module. See [Services](../reference/services.md).

## Putting it together

```
source text → parser → CST + AST → linking → validation
                         │             │          │
                         └─────────────┴──────────┘
                             CLI and editor features
```

Calling the parser alone builds the trees. A document build also prepares
symbols, resolves references and runs validation. Use the
[AST consumption guide](../learn/consume-the-ast.md) to choose between them.
