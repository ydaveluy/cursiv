# Debugging a Grammar

Hand-writing a PEG grammar is the most error-prone part of building a language: a rule may not match, the wrong alternative may win, an assignment may be missing, or the tree shape may come out wrong. Use this symptom → cause checklist and the tools below to see what the parser actually did.

## Inspect what happened

Parse a snippet with the public testing harness and look at the result:

```cpp
auto ws = pegium::testing::TestWorkspace::withShared<MyLanguageSharedServices>();
ws.registerLanguage(
    createMyLanguageServices(ws.shared<MyLanguageSharedServices>()));
auto document = pegium::testing::parse(ws, "my-language", "some input");
// document->parseResult.value : the produced AST node (null if nothing matched)
// document->parseSucceeded()  : whether the whole input was consumed
```

At the document level, read `document->diagnostics` (syntax diagnostics carry byte offsets) and check `document->parseSucceeded()` / `document->parseRecovered()` to tell a clean parse from one the parser recovered. Recovery is automatic, so it needs no configuration.

## Symptom → cause

| Symptom | Likely cause |
|---|---|
| A rule never matches | A keyword or terminal earlier in the sequence didn't match — check `.i()` case-sensitivity, the `ID` terminal's character range, and the skipper. |
| The wrong alternative wins | PEG choice `\|` is ordered and greedy: the first matching alternative wins, with no backtracking afterwards. Put the more specific / longer alternative first, or add a `!lookahead` guard. |
| A field is empty after parsing | The value matched but was not assigned. Matching alone does not populate the AST — wrap it in `assign<&T::field>(...)` or `append<&T::vec>(...)`. |
| The tree shape is wrong | Use `create<T>()` / `nest<&T::member>()` to control where nodes are built, and make sure repeated members use `append`, not `assign`. |
| A named node isn't found by name | It doesn't derive `pegium::NamedAstNode` (the only type the default name provider names), or its grammar doesn't assign `name`. Derive `NamedAstNode` **and** assign `name` — see [FAQ and Common Pitfalls](../faq.md). |
| A list eats too much | A `many(...)` / `some(...)` is consuming a following section. Add a negative lookahead (`!"nextKeyword"_kw`) to stop it, as the requirements grammar's string-literal rule does (`many(!"\""_kw + dot)`). |
| Operator precedence is wrong | `InfixRule` operator levels are declared highest-precedence first; reorder the `LeftAssociation(...)` / `RightAssociation(...)` levels. |

## Tips

- Build the grammar bottom-up: get terminals and the smallest rules passing in unit tests before composing the entry rule.
- Keep one assertion per rule while iterating ([Test Your Language](../learn/test-your-language.md)).
- If a rule is hard to get right, the AST shape may be fighting the grammar — adjusting the AST is often the real fix.

## Related pages

- [Grammar Essentials](grammar.md)
- [Grammar Reference](../reference/grammar-reference.md)
- [Test Your Language](../learn/test-your-language.md)
