# What is cursiv++?

cursiv++ turns a language definition into a command-line tool or an editor
extension. You write the abstract syntax tree (AST) types, the grammar and
the validation checks. Cursiv supplies parsing, reference resolution and
editor services.

A grammar is a `struct` deriving from `cursiv::GrammarSpec`. Its rules bind
directly to members of your AST structs, and the C++ compiler and the
generator check those bindings. CMake generates the parser when you build.
To add a language to an existing project, see
[CMake integration](../reference/cmake.md).

## Choose the components you need

| Component | Purpose |
|---|---|
| Core | Parsing, AST construction, references, scoping and validation |
| CLI (command-line interface) | Validate files from the terminal or a script |
| LSP (Language Server Protocol) | Add editor completion, navigation and rename |
| MCP (Model Context Protocol) | Let an AI assistant inspect and validate documents |

The scaffold includes all four. A program that only reads and validates files
needs only the core.

## Next

- [Mental model](mental-model.md): syntax trees, references and services.
- [Create a project](../learn/scaffold.md): build and run your first language.
- [Features](features.md): available parser and language-tool capabilities.
- [Why cursiv](why-cursiv.md): the benefits and costs of a grammar written in C++.
