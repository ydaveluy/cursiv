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

Yes, but a repeated item must read input
([`nullableRepetition`](reference/refusals.md#nullablerepetition)), and rules
may not call each other in a cycle that reads no text
([`unitProductionCycle`](reference/refusals.md#unitproductioncycle)):

```cpp
struct Item : rule<option(ref<ID>)> {};
struct List : rule<many(ref<Item>)> {};   // refused
```

For an optional list, make each item read input and let `many` allow zero
items: `struct Item : rule<ref<ID>> {};`.

### My keyword matches inside a longer word

Write words with `kw<>` or `ikw<>`, not `lit<>`. A keyword matches only when
the next character cannot continue a word: a letter, a digit or `_` by
default. If your identifiers contain other characters, set `word_boundary` in
the grammar. For identifiers that may contain `-`:

```cpp
static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;
```

### A reference resolves in the editor but not on the command line

The CLI loads only the named file unless you pass `--root <dir>`; see
[Load a workspace](reference/cli.md#load-a-workspace).

### Can a member be a `std::string` or a `std::vector`?

No. Use `string_view` for text and `cursiv::vector<T>` for lists; see
[AST vocabulary](reference/ast.md).

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
[Cross-compiling to wasm](recipes/wasm.md). Native builds are tested on Linux
only.

### What C++ do I need?

C++20, with a compiler that supports `std::format` and guaranteed tail calls
(`musttail`). The tested compilers are GCC 16 and Clang 22, and wasi-sdk 34
for WebAssembly. Older versions are not validated.
