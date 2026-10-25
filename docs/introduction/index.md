# What is cursiv++?

cursiv++ helps you turn a language definition into a command-line tool or an
editor extension. You write the data types for your abstract syntax tree
(AST), the grammar and the validation checks. Cursiv supplies parsing,
reference resolution and editor services.

A grammar is a `struct` deriving from `cursiv::GrammarSpec`. Its rules bind
directly to members of your AST structs. The C++ compiler and generator check
that those bindings are valid.

## Build your language

Edit your AST and grammar headers, then build with CMake. Parser generation
is automatic. The generated files go under the build directory; do not edit
them. See [CMake integration](../reference/cmake.md) when adding a language to
an existing project.

## Choose the components you need

| Component | Purpose |
|---|---|
| Core | Parsing, AST construction, references, scoping and validation |
| CLI (command-line interface) | Validate files from the terminal or a script |
| LSP (Language Server Protocol) | Add editor completion, navigation and rename |
| MCP (Model Context Protocol) | Expose language tools to a model |

The scaffold includes all four. A program that only reads and validates files
can use the core services directly.

## Next

- [Mental model](mental-model.md): syntax trees, references and services.
- [Create a project](../learn/scaffold.md): build and run your first language.
- [Features](features.md): available parser and language-tool capabilities.
- [Why cursiv](why-cursiv.md): the benefits and costs of a grammar written in C++.
