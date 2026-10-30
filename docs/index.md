# cursiv++

cursiv++ is a C++20 framework for building domain-specific languages and
their tools: parsers, validators, command-line tools and language servers
with Language Server Protocol (LSP) support. Its goal is to be as fast as
possible while staying simple to use: the standard language features work out
of the box, and you can customize every service.

![The Statemachine example in an editor: completion, an error and its fix, hover and rename](assets/demo.gif#only-light)
![The Statemachine example in an editor: completion, an error and its fix, hover and rename](assets/demo-dark.gif#only-dark)

[Try it online](demo/index.md): write documents in the example languages in
your browser, with their language servers compiled to WebAssembly.

You define the syntax and the abstract syntax tree (AST) in C++. Cursiv
generates the parser, resolves references, runs your validation checks and
provides editor features. For example, this rule reads the keyword `person`
followed by an identifier (`ID`), and stores the identifier in
`ast::Person::name`:

```cpp
struct Person : rule<kw<"person"> + assign<&ast::Person::name>(ref<ID>)> {};
```

The [tutorial](learn/walkthrough.md) builds a complete language this way.

Performance comes first: the generated parsers and the editor features stay
fast on large documents and workspaces. See the
[benchmarks](benchmarks/README.md).

## Start here

<div class="grid cards" markdown>

-   **New to cursiv**

    ---

    Understand the grammar, syntax trees and language services.

    [:octicons-arrow-right-24: Introduction](introduction/index.md)

-   **Get a language running**

    ---

    Scaffold a project, then take it from a grammar to an editor extension.

    [:octicons-arrow-right-24: Learn](learn/index.md)

-   **Solve one problem**

    ---

    Add scoping, validation, formatting or several languages to a project.

    [:octicons-arrow-right-24: Recipes](recipes/index.md)

-   **Look something up**

    ---

    Find a grammar operator, AST type, service or CMake function.

    [:octicons-arrow-right-24: Reference](reference/index.md)

</div>

## Create your first project

With the [required tools](learn/index.md#before-you-start) installed, run:

```bash
curl -fsSLO https://raw.githubusercontent.com/ydaveluy/cursiv/main/docs/cursiv-new.cmake
cmake -DNAME=MyLang -P cursiv-new.cmake
cd mylang
cmake -B build
cmake --build build --parallel
./build/src/mylang_cli example/hello.mylang
```

You get a working grammar, AST, CLI, language server and VS Code extension.

[Create a project :octicons-arrow-right-24:](learn/scaffold.md){ .md-button .md-button--primary }
