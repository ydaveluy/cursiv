# FAQ

### Do I need to write a grammar file?

No. There is no separate grammar language: you declare a `struct` that
derives from `cursiv::GrammarSpec` in a C++ header. See the
[tutorial](learn/walkthrough.md).

### Where does the generated code go, and do I commit it?

It goes in the `OUT` directory of
[`cursiv_generate_language`](reference/cmake.md#cursiv_generate_language). It
is build output: do not edit or commit it. A normal build regenerates it when
the grammar changes; there is no separate generate step.

### Why does my rule not build the AST class I expected?

A rule builds the most derived class that declares the members it writes.
A call to an AST rule that is not inside `assign` or `append` returns that
rule's node instead. Use `create<T>` to build another class, or to build a
node from a rule that writes no member. See
[Building the tree](reference/grammar.md#building-the-tree).

### Can a rule match the empty string?

Yes. A rule can match empty input. Each turn of an unbounded repetition
should consume input, and a cycle of rule calls must consume input before
returning to the same position. See [grammar errors](reference/refusals.md#rules-and-repetitions).

For a list of names, `many(ref<ID>)` accepts an empty list when `ID` consumes
at least one character.

### My keyword matches inside a longer word

Use `kw<"if">` or `ikw<"if">` for a word. With the default boundary,
`if2` is one identifier because the digit continues the word. If your
identifiers also allow `-`, include it in `word_boundary`:

```cpp
static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;
```

### A reference resolves in the editor but not on the command line

The CLI loads only the named file unless you pass `--root <dir>`; see
[Load a workspace](reference/cli.md#load-a-workspace).

### Can a member be a `std::string` or a `std::vector`?

Yes. `string` and `vector<T>` are those types. Prefer `string_view` for text when
you can; see [AST vocabulary](reference/ast.md).

### How do I test editor features?

Use the editor helpers of `cursiv::test`, such as `expectCompletion` and
`expectHover`, and mark the cursor with `<|>` in the test text. See
[Test your language](learn/testing.md).

### Why is my formatter not used?

There is no default formatter. Install one in `install_lsp_module`; the
server then offers formatting to the editor. See
[Formatting](recipes/formatting.md).

### Does the server run on Windows and macOS?

Ship the WebAssembly server: the VS Code extension runs it with Node.js, on
every platform VS Code supports. See
[Cross-compiling to wasm](recipes/wasm.md). The native CI builds and runs
tests on Linux, Windows (MSYS2 UCRT64) and macOS (Apple Clang).

### What C++ do I need?

C++20, with a compiler that supports `std::format` and guaranteed tail calls
(`musttail`). The tested compilers are GCC 16 and Clang 22, and wasi-sdk 34
for WebAssembly. Older versions are not validated.
