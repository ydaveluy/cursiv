# Features

## Parsing and syntax trees

Define syntax with C++ grammar rules and fill your abstract syntax tree (AST)
with `assign` and `append`. Cursiv provides LL and LR parsers; the default is
LR. See the [grammar reference](../reference/grammar.md) for operators and
supported forms.

## Ambiguity diagnostics

The generator reports unsupported grammar constructions before you run the
language. The LR backend can also report parser conflicts and overlapping
tokens, with examples that help identify the ambiguity.

## Error recovery that reports what it repaired

The parsers report syntax errors and attempt to retain a usable tree, so
editor features can keep working while a document is incomplete. The parse
result records whether recovery was needed; diagnostics describe the errors.

## Cross references and scoping

Connect names to declarations with `reference<Person>`. Cursiv resolves the
references after parsing. You can customize which declarations are visible,
for example to support packages and imports. `multi_reference<T>` lets one
name refer to a group of declarations.

## Validation

Register validation methods for AST types. Diagnostics can target a node or
a specific member: `.property<&ast::Person::name>()` underlines the name.

## Editor features

The Language Server Protocol (LSP) connects your language to editors. Default
services provide completion, hover, go-to-definition, find-references, rename,
symbols, folding and semantic tokens. You can replace individual
providers. A formatter is language-specific and must be supplied separately.
See [Services](../reference/services.md).

## A command line

The scaffold includes a command-line interface (CLI) that parses, links and
validates files. It can dump the concrete syntax tree (CST) as text or JSON
and load a workspace for cross-file references.
Its exit code makes it suitable for scripts and CI. See
[Command line](../reference/cli.md).

## Tools for models

Model Context Protocol (MCP) tools can validate draft text, inspect a workspace
and navigate references. Through the editor's bridge they see unsaved buffers;
a standalone server works with saved files. See
[MCP server](../reference/mcp.md).

## Native and WebAssembly deployment

The language server can compile to WebAssembly for distribution in a VS Code
extension with a Node.js runner. Native builds remain available.
See [WebAssembly](../recipes/wasm.md) for the build and runtime requirements.

## A working project template

`cursiv-new.cmake` creates a project with a grammar, AST, validators, tests,
CLI and editor client. Follow [Create a project](../learn/scaffold.md) to
build and run it.
