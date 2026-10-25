# cursiv++

cursiv++ is a C++20 framework for building languages, compilers and language
tools, with built-in Language Server Protocol (LSP) support.

Define the syntax and abstract syntax tree (AST) in C++. Cursiv generates a
parser and provides services for references, validation and editor features.
For example, this
rule reads a `person` declaration and stores its name in an AST member:

```cpp
struct PersonRule
    : rule<kw<"person"> + assign<&ast::Person::name>(ref<ID>)> {};
```

`ID` matches an identifier and `ast::Person` is your AST type. The
[tutorial](learn/walkthrough.md) shows a complete language with these pieces.

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

Use the compiler and build tools listed in [Create a project](learn/scaffold.md).

```bash
curl -fsSLO https://raw.githubusercontent.com/ydaveluy/cursiv/main/docs/cursiv-new.cmake
cmake -DNAME=MyLang -P cursiv-new.cmake
cd mylang
cmake -B build
cmake --build build --parallel
./build/src/mylang_cli example/hello.mylang
```

The project includes a working grammar, AST, CLI, language server and VS Code
extension. The [project guide](learn/scaffold.md) explains which files to edit.

[Create a project :octicons-arrow-right-24:](learn/scaffold.md){ .md-button .md-button--primary }
