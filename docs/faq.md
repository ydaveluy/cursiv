# FAQ

### Do I need to write a grammar file?

Declare a `struct` deriving from `cursiv::GrammarSpec` in a C++ header. See
the [tutorial](learn/walkthrough.md) for a complete grammar.

### Where does the generated code go, and do I commit it?

In the `OUT` directory passed to `cursiv_generate_language`. A relative `OUT`
is placed under `generated-src/` in the build directory. Generated sources
are build output: do not edit or commit them. The build regenerates them
when the grammar changes.

### Do I have to run a generate step?

Build normally with `cmake --build build --parallel`. CMake regenerates the
parser when needed.

### Why does my rule not build the AST class I expected?

A rule's class is deduced from the members its directives name, including
members written inside repetitions. An unassigned call to an AST rule returns
that rule's node instead. To create a node with no assigned members, use
`create<T>`. You can also use it to select a derived class explicitly. See
[Building the tree](reference/grammar.md#building-the-tree).

### Can a rule match the empty string?

Yes. A repetition that can run more than once must consume input on each
iteration (`nullableRepetition`). Cycles of reductions that consume no input
are also refused (`unitProductionCycle`). For an optional list, let `many`
allow zero items and make each item consume input:

```cpp
struct Item : rule<option(ref<ID>)> {};
struct List : rule<many(ref<Item>)> {};   // refused
```

Use a consuming item instead:

```cpp
struct Item : rule<ref<ID>> {};
struct List : rule<many(ref<Item>)> {};
```

### My keyword matches inside a longer word

Set `word_boundary` to the characters allowed to continue an identifier.
For example, identifiers containing only uppercase letters need
`static constexpr auto word_boundary = cr<"A-Z">;`. This controls where
`kw<>` and `ikw<>` may end, independently of case-insensitive matching.

### A reference resolves in the editor but not on the command line

Pass `--root <dir>` to load the folder containing the target declarations.
Without it, the CLI loads only the named file; the editor may already have
loaded the other workspace documents.

### Can a member hold a `std::string`?

No. Use `string_view` and keep the source text and `ParseResult` alive while
reading the tree.

### Can a list be a `std::vector`?

No. Use `cursiv::vector<T>`. Its elements must be trivially copyable;
use pointers for a list of nodes.

### How do I test what the editor answers?

Use the editor expectations in `cursiv::test`, with `<|>` marking the cursor
position. See [Test your language](learn/testing.md).

### Why is my formatter not used?

Formatting is language-specific and has no default implementation. Install a
formatter in your LSP module; the server then advertises the capability to the
editor. See [Formatting](recipes/formatting.md).

### Does the server run on Windows and macOS?

You can build a native server or distribute a WebAssembly server with the
Node runner. See
[Cross-compiling to wasm](recipes/wasm.md).

### What C++ do I need?

C++20, with a compiler that supports `std::format` and guaranteed tail calls
(`musttail`). CI tests GCC 16 and Clang 22, and cross-compiles with wasi-sdk 34.
Older compiler versions are not validated.
