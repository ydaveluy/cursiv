# Learn

Follow the first two guides in order. Use the others when you need them.

<div class="grid cards" markdown>

-   **[Create a project](scaffold.md)**

    ---

    Generate a project, build it and parse its sample document.

-   **[Build a language end to end](walkthrough.md)**

    ---

    Go from the hello-world grammar to references, checks and an editor, one
    change at a time.

-   **[Write a grammar](write-a-grammar.md)**

    ---

    Tokens, rules, lists, recursion and operator precedence, for a small
    calculator language.

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

VS Code needs [a few more tools](editor-integration.md). For WebAssembly,
follow the [WASI setup](../recipes/wasm.md).
