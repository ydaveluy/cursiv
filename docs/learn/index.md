# Learn

Start by creating a project, then follow the Recipe tutorial. The remaining
guides cover testing, using the AST and connecting an editor.

<div class="grid cards" markdown>

-   **[Create a project](scaffold.md)**

    ---

    Generate a project, build it and parse its sample document.

-   **[Build a language end to end](walkthrough.md)**

    ---

    From the hello-world grammar to cross references, checks and an editor —
    changing one thing at a time.

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
guaranteed tail calls (`musttail`). GCC 16 and Clang 22 are tested versions.

The [editor guide](editor-integration.md) lists the additional tools needed for
VS Code. For WebAssembly, follow the [WASI setup](../recipes/wasm.md).
