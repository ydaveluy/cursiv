# Learn

Start with the first two guides, in order. Read the others when you need them.

<div class="grid cards" markdown>

-   **[Create a project](scaffold.md)**

    ---

    Generate a project, build it and parse its sample document.

-   **[Build a language end to end](walkthrough.md)**

    ---

    Turn the hello-world project into a recipe language with references, a
    check and an editor, one step at a time.

-   **[Write a grammar](write-a-grammar.md)**

    ---

    Write the grammar of a small calculator: tokens, rules, lists, recursion
    and operator precedence.

-   **[Test your language](testing.md)**

    ---

    Check parsing, references, diagnostics and editor features.

-   **[Read the AST from a program](consume-the-ast.md)**

    ---

    Parse, link and validate a document from your own program.

-   **[Ship an editor extension](editor-integration.md)**

    ---

    Run the VS Code client, customize a provider and package the extension.

</div>

## Before you start

You need Git, CMake 3.30 or newer and a C++20 compiler with `std::format` and
guaranteed tail calls (`musttail`). GCC 16 and Clang 22 are tested.

The VS Code extension needs [a few more tools](editor-integration.md). For
WebAssembly, follow the [WASI setup](../recipes/wasm.md).
