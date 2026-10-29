# Features

## Parsing and syntax trees

Grammar rules are C++ types that fill your abstract syntax tree (AST) with
`assign` and `append`. Cursiv generates an LL or an LR parser; LR is the
default and recommended one. See the [grammar reference](../reference/grammar.md).

## Performance

Cursiv aims to be as fast as possible: the generated parsers and the
language services are compiled C++, and a workspace builds its documents in
parallel. See [Benchmarks](../benchmarks/README.md).

## Ambiguity diagnostics

The generator rejects unsupported grammar constructions when you build. The
LR generator also reports parser conflicts and overlapping tokens, with
examples that show the ambiguity.

## Error recovery

The parsers report syntax errors and keep a usable tree, so editor features
keep working while a document is incomplete. The parse result tells whether
recovery was needed.

## Cross references and scoping

`reference<Person>` connects a name to a declaration; Cursiv resolves it after
parsing. You can choose which declarations are visible, for example to
support packages and imports. `multi_reference<T>` lets one name refer to a
group of declarations.

## Validation

Register validation methods for AST types. A diagnostic can underline a whole
node or one member: `.property<&ast::Person::name>()` underlines the name.

## Editor features

Default Language Server Protocol (LSP) services provide completion, hover,
go-to-definition, find-references, rename, symbols, folding and semantic
tokens. You can replace any provider. Formatting needs a formatter written
for your language. See [Services](../reference/services.md).

## A command line

The scaffold's command-line interface (CLI) parses, links and validates
files. It can dump the concrete syntax tree (CST) as text or JSON and load a
workspace for cross-file references. Its exit code suits scripts and CI. See
[Command line](../reference/cli.md).

## Tools for models

Model Context Protocol (MCP) tools validate draft text, inspect a workspace
and navigate references. See [MCP server](../reference/mcp.md).

## Native and WebAssembly deployment

The language server builds natively or as WebAssembly. A VS Code extension
runs the WebAssembly server through Node.js. See [WebAssembly](../recipes/wasm.md).

## A working project template

`cursiv-new.cmake` creates a project with a grammar, AST, validators, tests,
CLI and editor client. See [Create a project](../learn/scaffold.md).
