# cursiv++

cursiv++ is a C++20 framework for building languages, compilers and language
tools, with built-in Language Server Protocol (LSP) support.

You define the syntax and the abstract syntax tree (AST) in C++. Cursiv
generates the parser and provides references, validation and editor
features. For example, this rule reads a `person` declaration and stores its
name, read by the `ID` terminal, in your `ast::Person` type:

```cpp
struct Person : rule<kw<"person"> + assign<&ast::Person::name>(ref<ID>)> {};
```

The [tutorial](learn/walkthrough.md) builds a complete language this way.

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

    Scoping, validation, formatting, several languages in one project.

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
