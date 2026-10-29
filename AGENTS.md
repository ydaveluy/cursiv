# Cursiv for Agents

This is a C++20 language-engineering framework with LL and LR parser backends.

For project documentation, read:

- `README.md`: Build, tests, benchmarks, and WebAssembly.
- `docs/reference/grammar.md`: Grammar DSL and parser contracts.
- `docs/reference/ast.md`: AST types and construction.
- `docs/reference/services.md`: Services, linking, workspaces, and LSP.
- `docs/reference/test.md`: Language testing helpers.

Source directories:

- `src/cursiv/core/`: Grammar model, syntax trees, linking, and workspaces.
- `src/cursiv/engines/`: LL/LR generators, runtimes, and shared code.
- `src/cursiv/cli/`: Command-line support for language tools.
- `src/cursiv/lsp/`: Language-server services.
- `src/cursiv/mcp/`: Model Context Protocol server and language tools.
- `src/cursiv/test/`: Parse, validation, and editor test helpers.
- `src/cursiv/bench/`: Benchmark harness and performance counters.
- `src/cursiv/fuzz/`: Grammar-based input generation and tree checks.
- `examples/`: Complete example languages.
- `docs/demo/`: The "Try it online" page of the documentation.
- `tests/`: Framework tests.
- `bench/`: Benchmark executables and corpora.
- `bench/recovery_metric/`: Error-recovery quality reports and internal support.
- `templates/`: Scaffolding templates for new language projects.
- `cmake/`: Build integration and language generation.
- `tools/`: Build, packaging, and development scripts.

## VS Code extensions

`examples/*/vscode/` contains TypeScript extension projects, built with npm.
See each extension's `package.json` and `docs/learn/editor-integration.md`.
