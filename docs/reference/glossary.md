# Glossary

**AST (abstract syntax tree)** — your language's parsed objects: C++ structs
whose members hold names, values, child nodes and references.

**CST (concrete syntax tree)** — the parsed tokens and their source positions,
including comments declared as `hidden`. Editor features use it to locate
text corresponding to AST nodes and members.

**Data rule** — a rule that returns a value rather than an AST node. By
default, it joins its tokens' text without trivia.

**Directive** — an operation that builds or fills an AST node: `create`,
`assign`, `append`, `flag` or `nest`. `skip` controls trivia instead.

**Document** — a source text with a URI, parsed trees, diagnostics and symbols
available for linking to other documents.

**Entry** — the rule a document starts at, named by `using entry = R;`.

**Generated module** — the parser, AST metadata and service setup produced
from your grammar. These files are build output.

**hidden / ignore** — trivia settings. `hidden` retains tokens in the CST,
for example to associate comments with declarations. `ignore` discards them.

**Index** — metadata describing exported symbols across the workspace,
used to resolve cross-document references.

**Keyword** — a literal with a word-boundary check. `kw<"if">` matches `if`
but not the beginning of `iffy`.

**Linker** — resolves a reference's written name against a scope, after the
parse.

**Reference** — a member that reads a name at parse time and is resolved
afterwards. `reference<T>`, or `multi_reference<T>` for one binding to a group.

**Reflection** — metadata that lets services inspect AST types and members
without knowing each concrete C++ class.

**Refusal** — a grammar rejected during compilation or parser generation.
See [Grammar errors](refusals.md).

**Formatting region** — syntax nodes selected for a spacing or indentation rule.

**Rule** — an expression that combines tokens, allows trivia between them,
and returns an AST node or a value. Declare one with `rule<...>`.

**Scope** — the symbols available as targets for a reference at a particular
position. `scopeComputation` collects local and exported symbols;
`scopeProvider` selects the candidates visible at that position.

**Services** — framework objects such as the parser, linker and validator.
Shared services operate across a workspace; language services belong to one
registered language.

**Terminal** — an expression recognizing one token, with no trivia between
its parts. Declare one with `terminal<...>`.

**Trivia** — whitespace and comments: what `ignore` and `hidden` declare.

**Word boundary** — the character class that can continue a name, so `kw<"if">`
does not match inside `iffy`. Set with `word_boundary`; its default is `w`.
