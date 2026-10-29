# Glossary

**Adoption** — an unassigned call to an AST rule returns that rule's node:
`lit<"("> + ref<Expression> + lit<")">` returns the `Expression` node. See
[Adoption and classes](grammar.md#adoption-and-classes).

**AST (abstract syntax tree)** — your language's parsed objects: C++ structs
holding names, values, child nodes and references.

**Category** — of a token: keyword, literal or pattern. At equal length,
keywords and literals beat patterns. See [Tokens](grammar.md#tokens).

**Condition** — `&e` or `!e`: a test of the next tokens (in a rule) or the
adjacent bytes (in a terminal) that consumes and builds nothing. See
[Conditions](grammar.md#conditions).

**Construction context** — where one value is built: each rule call and each
operand of `assign` or `append` has its own. See
[Nested assignments](grammar.md#construction-contexts).

**CST (concrete syntax tree)** — the parsed tokens with their source
positions, including `hidden` comments. Editor features use it to locate AST
nodes and members in the text.

**Current node** — the node that directives write. `create` gives it a class,
`nest` wraps it in a new current node, and an adopting call makes the called
rule's node current.

**Data rule** — a rule that returns a value rather than an AST node: the value
of the rule or branch it reads alone, or else its tokens' text without trivia.

**Directive** — `create`, `assign`, `append`, `flag` or `nest`: an operation
that builds or fills an AST node. `skip` controls trivia instead.

**Document** — a source text with a URI, its parsed trees, diagnostics and
symbols.

**Entry** — the rule a document starts at, named by `using entry = R;`.

**Formatting region** — syntax nodes selected for a spacing or indentation rule.

**Fragment** — a named expression, `static constexpr auto f = e;`, written in
place wherever `f` is used; its directives write the using rule's current
node. See [Reuse grammar pieces](grammar.md#reuse-grammar-pieces).

**Generated module** — the parser, AST metadata and service setup generated
from your grammar, as build output.

**hidden / ignored** — trivia settings: `hidden` keeps tokens in the CST, for
example to attach comments to declarations; `ignored` discards them.

**Index** — the workspace's exported symbols, used to resolve references
across documents.

**Keyword** — a literal with a word-boundary check: `kw<"if">` matches `if`
but not the start of `iffy`.

**Linker** — resolves a reference's written name against a scope, after the
parse.

**Path** — one way through a rule's body: one alternative at each choice, one
number of turns at each option or repetition. A single-valued member is
written at most once per node and path.

**Reference** — a member that reads a name at parse time and is resolved
afterwards: `reference<T>`, or `multi_reference<T>` for several targets.

**Reflection** — metadata that lets services inspect AST types and members
without knowing each C++ class.

**Refusal** — a grammar error: no parser is generated. See
[Grammar errors](refusals.md). Input a parser does not accept is *rejected*.

**Rule** — an expression that combines tokens, allows trivia between them, and
returns an AST node or a value. Declared with `rule<...>`.

**Scope** — the symbols a reference can target at a position, collected by
`scopeComputation` and selected by `scopeProvider`.

**Services** — framework objects such as the parser, linker and validator,
shared by the workspace or owned by one language.

**Terminal** — an expression recognizing one token, with no trivia inside.
Declared with `terminal<...>`.

**Trivia** — whitespace and comments, declared with `ignored` or `hidden`.

**Viable parse** — a parse that has read the text so far without error. The
lexer only tries the terminals a viable parse expects, plus trivia. See
[Tokens](grammar.md#tokens).

**Word boundary** — the character class that continues a word, set with
`word_boundary` (`w` by default).
