# Parser engines

These instructions apply to LL/LR generators, runtimes and shared engine code.
Performance and simplicity are core design requirements; preserve parser
correctness while improving both.

Source directories, relative to `src/cursiv/engines/`:

- `ll/gen/`: Grammar lowering, LL analysis, table and C++ code generation.
- `ll/runtime/`: LL parsing, prediction, recovery and completion.
- `ll/table/`: LL program and table representations.
- `lr/gen/`: Grammar lowering, LR automata, table and C++ code generation.
- `lr/runtime/`: LR parsing, lookahead, graph-structured stacks, recovery and
  completion.
- `lr/table/`: LR tables, actions and productions.
- `shared/gen/`: Shared grammar, lexer and AST generation utilities.
- `shared/runtime/`: Shared lexing, predicates, indentation and recovery helpers.
- `shared/table/`: Shared grammar, symbol and lexical data representations.

## Algorithms and memory

- Eliminate avoidable quadratic work: repeated full scans, linear membership
  checks inside loops, and repeated copying of growing histories or stacks.
  Prefer linear or logarithmic bookkeeping and examine worst-case growth.
- Prefer iterative traversals with explicit stacks or worklists. Grammar and
  input depth must not determine native, WASM or JavaScript call-stack usage.
- Minimise allocations, temporary copies and retained data in hot paths.
  Share prefixes and continuations where useful; account for peak memory
  during container growth, not only the final retained size.
- Keep generated code compact. Control table sizes, function sizes and
  repeated instantiations: generation, C++ compilation and WASM/JIT compilation
  are part of the performance cost.

## Simplicity and factorisation

- Choose the simplest algorithm that meets correctness and performance needs.
  Remove redundant state and passes before adding caches or special cases.
- Factor common logic between LL and LR when their contracts match. Keep
  backend differences explicit; avoid generic layers that add indirection,
  duplicate representations or obscure invariants.
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
