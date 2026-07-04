# pegium tests & test APIs

Two — and only two — testing namespaces exist. They sit at different layers and
serve different audiences; pick by *what you are testing*, not by habit.

## `pegium::testing` — the public, shipped harness

- **Lives in** `src/pegium/testing/` and is built as the reusable library
  `pegium::testing` (installed; downstream projects link it directly).
- **Layer:** feature-level, GoogleTest-coupled. Parse a marked-up source string
  (`<|>` cursor, `<| … |>` range) and assert on validation and LSP features:
  `TestWorkspace` + `expectCompletion` / `expectHover` / `expectGoToDefinition` /
  `expectFindReferences` / `expectHighlight` / `expectFormatting` /
  `expectSymbols` / `expectSemanticToken` / `expectError` / `expectWarning` …
- **Audience:** authors of pegium-based *languages* (e.g. downstream grammars),
  and pegium's own end-to-end LSP-feature tests. The feature dispatch is hidden
  inside the compiled library, so consumers never include pegium internals.

Use it whenever the feature-level assertion is what you mean — it is the
first-class way to test "does completion/hover/… behave correctly for this
grammar".

## `pegium::test` — the internal test support

- **Lives under** `tests/` (header-only support next to the suites; **not**
  shipped or installed).
- **Layer:** mechanism-level harnesses for exercising pegium's *own internals* —
  the CST builder (`TestCstBuilderHarness`), single-rule parsing
  (`TestRuleParser`), the recovery engine (`RecoveryHarnessTestSupport`,
  `RecoveryCacheNeutralityHarness`), service/document construction
  (`CoreTestSupport`, `ParseSupport`), fuzz sweeps (`KeywordFuzzHarness`),
  and per-area fixtures (cache keys, validation nodes, navigation models,
  the JSON-converter parser …).
- **Audience:** pegium's own unit/integration tests only.

Everything internal now lives under the single `pegium::test` namespace — do not
introduce siblings like `pegium::test_navigation` or module-scoped
`pegium::<module>::test_support`; add fixtures to `pegium::test` and give them
distinct names.

## Rule of thumb

| You are writing… | Use |
|---|---|
| a language / LSP-feature test (any grammar, downstream or in-tree) | `pegium::testing::expect*` |
| a test of a pegium internal (parser recovery, CST build, cache, reflection…) | `pegium::test` |

`pegium::bench` and `pegium::fuzz` are separate benchmark / fuzz harnesses, not
assertion APIs.
