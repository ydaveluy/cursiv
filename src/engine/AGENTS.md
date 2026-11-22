# Parser engine

These instructions apply to the parser engine: the generator that builds a
language's parser and the runtime that parser runs. Performance and simplicity
are core design requirements; preserve parser correctness while improving both.

The engine has one include root, `src/engine/`, with its sources in
`cursiv/engine/` below it. It reads `src/core`; nothing in `src/core` reads it.

Source directories, relative to `cursiv/engine/`, one per theme:

- `grammar/`: The grammar as the engine reads it, and its lowering.
- `lexer/`: Character classes and the lexer automaton.
- `parser/`: LR automaton, table packing and writing, and the parse.
- `recovery/`: The repair of a text the parser refuses.
- `completion/`: What may be written at a place in a text.
- `ast/`: The tree builder, its typed actions, and the AST tables.
- `generator/`: What a language's generator runs and the module it writes.

Code that runs when a parser is generated is in namespace
`cursiv::engine::gen`; code a parser runs is in `cursiv::engine`.

## Algorithms and memory

- No quadratic work, in the generator or at run time: no repeated full
  scans, linear membership checks inside loops, pairwise passes over states
  or terminals, or repeated copying of growing histories or stacks. Use
  linear or logarithmic bookkeeping and check the worst case of every nested
  loop. Bound work by sharing it, each state, goto site or token read once,
  not by a cap. Conflict resolution runs to a decision or caller cancellation.
- No recursion: traverse with explicit stacks or worklists. Grammar and input
  depth must not determine native, WASM or JavaScript call-stack usage.
- Minimise allocations, temporary copies and retained data in hot paths.
  Share prefixes and continuations where useful; account for peak memory
  during container growth, not only the final retained size.
- Keep generated code compact. Control table sizes, function sizes and
  repeated instantiations: generation, C++ compilation and WASM/JIT compilation
  are part of the performance cost.

## Simplicity and factorisation

- Choose the simplest algorithm that meets correctness and performance needs.
  Remove redundant state and passes before adding caches or special cases.
- One mechanism per concern; avoid generic layers that add indirection,
  duplicate representations or obscure invariants.
- A change replaces what it supersedes: extend or rewrite the existing
  mechanism instead of adding a second one beside it, and delete in the same
  change the code, fields and tables it leaves unused.
- Keep types and constants consistently defined across translation units.
  Isolate generated helpers in their grammar's namespace to prevent ODR
  violations. Hashes may select candidates; equality must remain exact.

## Validation

- Support performance changes with targeted measurements or work/allocation
  counters. Check scaling on deep, long, repetitive and ambiguous inputs.
- Reuse existing grammars and test executables for regressions. Preserve AST,
  CST, lexical election, recovery and completion contracts.
- Fix algorithmic causes rather than increasing resource limits or weakening
  stress tests. Run checks relevant to the change, including WASM when its
  memory or execution constraints matter.
