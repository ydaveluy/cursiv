# Mental model

Four ideas help you write a grammar, read its results and customize language
features.

## 1. Your headers define the language

You write the abstract syntax tree (AST) structs and the grammar rules that
fill them. The CMake build generates the parser and the language module:

```
ast.hpp + MyLangGrammar.hpp → CMake build → parser and language module
```

Never edit the generated files. Change your headers and build again.

## 2. Two trees, and they answer different questions

The **CST** (concrete syntax tree) records the parsed text and its positions,
including comments declared as `hidden`. Editor services use it to find text
ranges and the token under the cursor.

The **AST** holds your structs and their values: names, child nodes, lists
and references. Your validation, interpreter or code generator works on it.

## 3. A reference is read, then resolved

A reference connects a written name to a declaration. The parser stores the
name; the **linker** then finds the declaration among the names visible at
that position, its **scope**. Declarations may come later in the file or in
another file.

```cpp
struct Greeting : cursiv::Extends<cursiv::AstNode> {
  reference<Person> person;   // written as a name, resolved later
};
```

By default, a named node is visible inside its container. For packages and
imports, see [Qualified names and scoping](../recipes/scoping.md).

## 4. Services live in two containers

A service implements one feature, such as parsing, validation or completion:

- **Shared services** manage the workspace's documents and exported names.
- **Language services** provide one language's parser, references,
  validation and editor features.

A workspace can hold several languages. To customize a feature, replace or
add a service in your language's module. See [Services](../reference/services.md).

## Putting it together

```
source text → parser → CST + AST → linking → validation
                         │             │          │
                         └─────────────┴──────────┘
                             CLI and editor features
```

The parser alone builds the trees. A document build also resolves references
and runs validation. [Read the AST from a program](../learn/consume-the-ast.md)
shows both.
