# Features

## Parsing and syntax trees

Grammar rules are C++ types that fill your abstract syntax tree (AST) with
`assign` and `append`. Cursiv generates an LR parser from them. See the
[grammar reference](../reference/grammar.md).

## Performance

Cursiv aims to be as fast as possible: the generated parsers and the
language services are compiled C++, and a workspace builds its documents in
parallel. See [Benchmarks](../benchmarks/README.md).

## Grammar checks

The generator rejects unsupported grammar constructions when you build. See
[Grammar errors and fixes](../reference/refusals.md). Where a text has several
parses, the grammar's order [chooses one](../reference/grammar.md#choosing-between-parses).

## Error recovery

The parser reports syntax errors and can return a tree for incomplete input.
Check the [parse result](../learn/consume-the-ast.md#parse-only) before reading
its root.

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
go-to-definition, find references, rename, symbols and folding. Install
`GrammarSemanticTokenProvider` for semantic highlighting or a formatter for
your language's layout. See [editor integration](../learn/editor-integration.md).

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
