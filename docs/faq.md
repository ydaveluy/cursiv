# FAQ

### Do I need to write a grammar file?

No separate grammar language: declare a `struct` deriving from
`cursiv::GrammarSpec` in a C++ header. See the [tutorial](learn/walkthrough.md).

### Where does the generated code go, and do I commit it?

It goes in the `OUT` directory of
[`cursiv_generate_language`](reference/cmake.md#cursiv_generate_language). It
is build output: do not edit or commit it. A normal build regenerates it when
the grammar changes; there is no separate generate step.

### Why does my rule not build the AST class I expected?

A rule's class is deduced from the members its directives name, and an
unassigned call to an AST rule returns that rule's node instead. Use
`create<T>` to create a node with no assigned members or to pick a derived
class. See [Building the tree](reference/grammar.md#building-the-tree).

### Can a rule match the empty string?

Yes, but a repetition that can run more than once must consume input on each
turn (`nullableRepetition`), and reductions may not cycle without consuming
input (`unitProductionCycle`):

```cpp
struct Item : rule<option(ref<ID>)> {};
struct List : rule<many(ref<Item>)> {};   // refused
```

For an optional list, let `many` allow zero items and make each item consume
input: `struct Item : rule<ref<ID>> {};`.

### My keyword matches inside a longer word

Set `word_boundary` to the characters that may continue an identifier, for
example `static constexpr auto word_boundary = cr<"A-Z">;` when identifiers
use only uppercase letters. It controls where `kw<>` and `ikw<>` may end.

### A reference resolves in the editor but not on the command line

The CLI loads only the named file unless you pass `--root <dir>`; see
[Load a workspace](reference/cli.md#what-root-changes).

### Can a member be a `std::string` or a `std::vector`?

No. Use `string_view` for text and `cursiv::vector<T>` for lists; see
[AST vocabulary](reference/ast.md).

### How do I test what the editor answers?

Use the editor expectations of `cursiv::test`, with `<|>` marking the cursor.
See [Test your language](learn/testing.md).

### Why is my formatter not used?

There is no default formatter. Install one in your LSP module and the server
advertises formatting to the editor. See [Formatting](recipes/formatting.md).

### Does the server run on Windows and macOS?

You can build a native server, or distribute a WebAssembly server with the
Node runner. See [Cross-compiling to wasm](recipes/wasm.md).

### What C++ do I need?

C++20, with a compiler that supports `std::format` and guaranteed tail calls
(`musttail`). The tested compilers are GCC 16 and Clang 22, and wasi-sdk 34
for WebAssembly. Older versions are not validated.
