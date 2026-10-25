# Grammar reference

Declare a grammar as a C++ type derived from `cursiv::GrammarSpec`. Inside it,
use terminals to recognize text, rules to combine tokens, and directives to
fill your AST.

Except for the complete quick-start example, snippets assume the surrounding
grammar and AST declarations, such as `ID`, `Expression` or the named members.

The syntax below applies to LL and LR, within their
[support limits](refusals.md#where-the-engines-differ). Examples show valid
input or rejection before error recovery; a parser may repair invalid input
and report diagnostics.

| To… | See |
|---|---|
| Match identifiers, numbers or comments | [Text](#text) |
| Combine rules or repeat an item | [Composition](#composition) |
| Fill AST members or wrap an expression | [Building the tree](#building-the-tree) |
| Convert text to numbers or other values | [Conversion](#conversion) |
| Define binary operators | [Operator precedence](#operator-precedence) |
| Control whitespace or indentation | [Layout](#layout) |
| Understand an error from the generator | [Grammar errors](refusals.md) |

## Quick start

```cpp
#include <cstdint>
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

struct Variable : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  int64 value = 0;
};

struct Module : cursiv::Extends<cursiv::AstNode> {
  vector<Variable*> variables;
};

struct Variables : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"//">, &eol | eof)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct INTEGER : terminal<some(d), std::int64_t> {};

  using ignore = WS;
  using hidden = COMMENT;

  struct VariableRule
      : rule<kw<"let"> + assign<&Variable::name>(ref<ID>) + lit<"="> +
             assign<&Variable::value>(ref<INTEGER>) + lit<";">> {};
  struct Document
      : rule<many(append<&Module::variables>(ref<VariableRule>))> {};
  using entry = Document;
};
```

This grammar produces the following AST:

| Input | Result |
|---|---|
| `let count = 12;` | A `Module` containing one `Variable` with `name = "count"` and `value = 12`. |
| `let x = 1; let y = 2;` | A `Module` containing two variables, in source order. |
| Empty text | A `Module` with an empty `variables` list. |

`assign` stores one value; `append` adds one element. The members named by
these directives determine each rule's AST class, including when a repetition
matches no items.

For a complete project with building, validation and editor support, follow
[Build a language end to end](../learn/walkthrough.md).

## Rules and grammar settings {#model}

A **terminal** recognizes one token, such as an identifier or number. A
**rule** combines tokens and may build an AST node. Call either with `ref<R>`.

| Declaration | Meaning |
|---|---|
| `terminal<Body>` | One token, matched against adjacent input with no trivia inside; its value is its text. |
| `terminal<Body, Conv>` | A token with a [converter](#conversion). |
| `rule<Body>` | Tokens, with trivia between them. |
| `rule<Body, Conv>` | A data rule whose text is converted. |
| `ref<R>` | A call to a terminal or rule. `ref<terminal<…>>` and `ref<rule<…>>` declare one anonymously in place. |
| `infix<Primary, &L, &Op, &R, Levels>` | Binary operators with declared [precedence](#operator-precedence). |
| `static constexpr auto f = e;` | A **fragment**: `e` written in place wherever `f` is used. |

An **AST rule** builds or returns one node. A **data rule** returns a value
instead: by default, the text of its tokens joined without whitespace or
comments. For example:

```cpp
struct QualifiedName : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
```

`a . b` gives the value `a.b`. A converter can turn a terminal's or data
rule's text into a number or another value; see [Conversion](#conversion).

| Setting | Meaning |
|---|---|
| `using entry = R;` | Read a whole document from `R`, trailing trivia included. Parsed on its own, `R` builds a node; a [section](#embedded-grammars)'s entry may be a data rule. |
| `using ignore = T;` or `list<T…>` | Trivia dropped from the CST. |
| `using hidden = T;` or `list<T…>` | Trivia retained in the CST, so comments can attach to what follows. |
| `static constexpr auto word_boundary = c;` | Class continuing a word; `w` by default. |
| `indent_open`, `indent_close`, `indent_newline` | [Indentation](#indentation) settings. |

Whitespace and comments are called **trivia**. They must consume input, and a
terminal can belong to `ignore` or `hidden`, never both. A rule allows trivia
between its tokens; a terminal requires adjacent text.

## Text

### Characters

A character class matches one character. For valid UTF-8, that means one
complete Unicode scalar; an invalid byte is treated as one character.
`any + any` therefore requires two characters and does not match a single
`é`. Use `byte` when you need to match raw bytes instead.

| Spelling | Reads |
|---|---|
| `cr<"a-zé_">` | one member of the class |
| `cr<"^…">`, `~c` | one character outside the class |
| `uc::L`, `uc::Lu`, `uc::Nd`, … | a Unicode general category or subcategory; `Cs` and `Cn` are not offered |
| `uc::XID_Start`, `uc::XID_Continue`, `uc::White_Space` | a Unicode property |
| `s`, `w`, `d`; `S`, `W`, `D` | ASCII shorthands and their complements |
| `any`; `byte` | one character; one raw byte |

For escaping, Unicode properties and invalid UTF-8, see
[Character class details](#character-class-details).

### Literals and keywords

| Spelling | Reads |
|---|---|
| `lit<"…">` | these exact bytes, never empty |
| `lit<"…", G>` | the literal if `G` holds immediately after its bytes: `lit<"=", !lit<"=">>` |
| `kw<"…">` | the literal unless the next character continues a word |
| `ikw<"…">` | the same, ignoring ASCII letter case only |
| `eol` | CRLF, LF, or CR; CRLF is indivisible |
| `eof` | zero-width end of input; in rules, after trailing trivia; in sections, at the closing boundary |

`kw<"if">` matches `if` but not the start of `iffy`. The `word_boundary`
setting defines which characters continue a word; its default, `w`, is
`a-zA-Z0-9_`. `ikw<"if">` also matches `IF` or `If`, using ASCII case folding.

`eol` does not match end of input. Use `eol | eof` when either is allowed.

`kw<"if">.doc<"…">()` adds hover text; different texts for one token are
refused. A Unicode language can set
`word_boundary = uc::XID_Continue | cr<"_$">`.

Keywords are contextual: `type` can match `ID` where only an identifier is
expected. Where the grammar also expects `kw<"type">`, the keyword takes
priority. Include the keyword in a name rule (`ref<ID> | kw<"type">`) to allow
it as a name there.

??? note "Reserved words and custom word boundaries"

    To reserve words everywhere, exclude them from the identifier terminal:
    `terminal<!(kw<"if"> | kw<"else">) + cr<"a-zA-Z_"> + many(w)>`.

    A keyword's boundary check matches the next complete character. A spelling
    whose last character cannot continue a word has no boundary check:
    `kw<"*">` is the same as `lit<"*">` with the default boundary. An `ikw`
    with no ASCII letter is refused.

### Terminals

Inside a terminal, a call to another terminal matches its text as part of the
same token. For example, `terminal<ref<ID> + lit<":"> + ref<ID>>` matches
`a:b` as one token. A rule with the same body matches three tokens and allows
trivia between them.

A terminal used as a token or as trivia must consume input, except for
`indent` and `dedent` as the whole body. A terminal used only in a condition
may match nothing. See [Recursive terminals](#recursive-terminals) for
recursive calls and their restrictions.

### Through

Use `through(open, close)` for delimited text, such as a comment:

```cpp
struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
```

On `/* a */ b */`, this token ends at the first `*/`. That choice does not
change if a later rule fails.

`through` can be used inside a terminal or directly in a rule, where it is
one anonymous token. It means `open + many(!close + any) + close`. A closer
that matches nothing ends it immediately. Use `close | eof` to allow an
unfinished delimiter; `through(lit<"//">, &eol | eof)` stops before the line
ending or at EOF.

## Composition

`a + b` matches `a`, then `b`; `a | b` matches either alternative.

```cpp
kw<"let"> + ref<ID>                         // a keyword followed by a name
kw<"public"> | kw<"private">                // either keyword
many(append<&Block::statements>(ref<Statement>)) // zero or more statements
```

Use parentheses to make mixed operators explicit. See
[Choosing between parses](#choosing-between-parses) when several alternatives
can match the same input.

### Repetition

| Form | Count |
|---|---|
| `option(e)`, `many(e)`, `some(e)` | Zero or one, zero or more, one or more |
| `many(e, sep)`, `some(e, sep)` | Zero or more, one or more, with separators between items |
| `repeat<N>(e)` | Exactly N, with N ≥ 1 |
| `repeat<Min, Max>(e)` | Min to Max; Min ≤ Max and Max ≥ 1; `kUnbounded` means no upper bound |

A trailing separator is explicit: `option(some(e, sep) + option(sep))`.

Put `append` inside a repetition to collect one child per item:
`many(append<&Block::statements>(ref<Statement>))`.

An item repeated more than once must consume input, including through rule
calls. For example, `many(option(x))` is
[refused](refusals.md#nullablerepetition); use `many(x)` instead. An option's
item may match nothing.

Nested repetitions remain separate: `many(some(x))` produces a warning but
is not simplified. When an item has several possible parses, repetition
follows [parse selection](#choosing-between-parses) rather than maximizing
the item count. For example, `many((lit<"a"> + lit<"a">) | lit<"a">)`
matches `aa` as one item because the item's first alternative has priority.

### Unordered groups

`a & b` matches each item once, in either order. Use optional items when some
may be absent:

| Expression | Accepted input |
|---|---|
| `a & b` | `a b` or `b a` |
| `option(a) & option(b)` | Empty text, either item, or both orders |
| `option(a & b)` | Empty text or both items in either order |
| `some(a) & b` | `a a b` or `b a a`, but not `a b a` |

An item that matches nothing still counts as visited. A repeated item stays
one continuous block; it cannot stop and resume around another item.

`a & b & c` forms one group; a rule call is one item. Permutations follow
written positions (`a b c`, `a c b`, `b a c`, …), under the
[parse selection rules](#choosing-between-parses).
Groups hold at most
[11 items, or 6 when one contains `nest`](refusals.md#unorderedtoowide).

### Choosing between parses

The lexer chooses tokens first. The parser then considers the alternatives
that can complete the input with those tokens. If several complete parses
remain, the grammar's order decides:

| Construct | Preferred decision |
|---|---|
| Choice | First written alternative |
| Option | Present |
| Repetition | Another item |
| Unordered group | Earliest permutation by written item positions |

The parser cannot recover a shorter token discarded by the lexer. For
example, `(lit<"a"> + lit<"b">) | (lit<"ab"> + lit<"c">)` rejects `ab`:
the lexer chooses `ab`, then the second alternative requires `c`. The rule
`lit<"a"> + lit<"b">` alone accepts it. See [Tokens](#tokens) for lexical
priority.

??? note "Ambiguous grammars: exact decision order"

    In a terminal, all matching alternatives remain possible until the whole
    token's longest match is selected. In rules, compare complete parses'
    decisions from left to right, with each expression's decision before its
    nested expressions. [Left recursion](#left-recursion) is ordered as a loop.
    The first different decision determines the selected parse.

    Factoring `A B | A C` into `A (B | C)` preserves accepted input but can
    change the selected tree:

    ```text
    A := x y | x       B := y z       C := z
    A (B | C)          parses x y z with A = x y, then C
    A B | A C          parses x y z with A = x, then B
    ```

    The first expression reaches `A`'s decision first; the second reaches its
    outer choice first. Likewise, `option(kw<"else"> + s)` after an `if` body
    associates `else` with the nearest `if`. Written as two alternatives with
    the `else` alternative first, it associates with the outer `if`.

## Building the tree

Use directives to store parsed values in your AST members. In the quick
start, `assign<&Variable::name>(ref<ID>)` stores the identifier in `name`, and
`many(append<&Module::variables>(ref<VariableRule>))` collects variables in a
list.

| Directive | Effect |
|---|---|
| `create<T>`, `create<T>(e)` | Gives the current node the class `T`, preserving earlier writes. `create<T>(e)` is `create<T> + e`. |
| `assign<&T::m>(e)` | Builds one value separately and stores it in the single-valued member `m`. |
| `append<&T::m>(e)` | Builds one value separately and adds it to the `vector` member `m`. |
| `flag<&T::m>(e)` | `e`, then sets the `boolean` member `m` to `true` on the node then current ([below](#writes-and-wrapping)). |
| `nest<&T::m>(e)`, `nest<W, &T::m>(e)` | Wraps the current node in a new `T`, or `W` derived from `T`, holding it in `m`; `e` writes into the wrapper. |
| `assign<xref<&T::m, N>>(e)` | Assigns a reference whose target must be an `N`. |

The AST class is usually inferred from the members named by the directives.
Use `create<T>` when you want to name it explicitly, such as for an empty
container or a derived class. Only the selected parse builds the AST;
abandoned alternatives do not write members. Every constructed node must be
returned by its rule or stored in an AST member.

### Adoption and classes

An unassigned call to an AST rule returns that rule's node. This is called
**adoption**: `lit<"("> + ref<Expression> + lit<")">` returns the same AST
node as `Expression`, without a wrapper for the parentheses. An `assign` or
`append` around the call stores that node in a member instead.

An adopting call that a path reaches after a write,
`create`, `nest` or adoption in the same context is refused: hold its node
with `assign` or `append`, or write the shared part as a fragment. After an
adoption, writes and `nest` may follow, but not a `create`.

??? note "Detailed class inference rules"

    A path that adopts nothing builds its **implicit node**, even when optional
    writes do not run. Its class is the `create<T>` when present; otherwise, the
    most derived class declaring its written members, with every other declaring
    class a base of it. Unrelated classes require an explicit `create<T>`.
    Within a construction context, a path holds at most one `create`, before any
    `nest`; a wrapper's class is the `W` of `nest<W, &T::m>`. `create` can follow
    writes:
    `assign<&Decl::name>(ref<ID>) + create<Function>(…)` builds a `Function`
    holding `name`.

    A pointer to an inherited member names its declaring class:
    `assign<&Derived::name>` names `Base` if `Base` declares `name`; use
    `create<Derived>` to build the derived class. `option(ref<Expression>)`
    alone is refused: its empty path has no class to build.

    The return class is the most derived common base of all possible final
    nodes, across choices and wrappers. For
    `ref<Name> + many(nest<&Index::array>(…))`, it is the common base of `Name`
    and `Index`; with `some`, it is `Index`.

To share writes into one node, use a fragment: its directives write the
current node of the rule that uses it, where calling an AST rule would build
an independent node. A common prefix then picks the class with `create`:

```cpp
static constexpr auto fn_body = kw<"fn"> + assign<&Fn::name>(ref<ID>) + …;
static constexpr auto var_body = kw<"var"> + assign<&Var::name>(ref<ID>) + …;
struct DeclRule : rule<option(flag<&Decl::exported>(kw<"export">)) +
                       (create<Fn>(fn_body) | create<Var>(var_body))> {};
```

### Writes and wrapping

A single-valued member is written **at most once per node and path**. This
includes writes in adopted nodes: a later write must not repeat a member the
adopted rule may write. `many(assign<&T::m>(…))` is refused unless a `nest`
makes a new node per turn; a rule called per turn has its own context.

`append` takes one element: use `many(append<&B::items>(ref<Item>))`, not
`append<&B::items>(many(ref<Item>))`. Directives inside a
[condition](#conditions), a `through` or a `mark` are refused: they would
never run.

`flag<&T::m>(e)` matches `e` and then sets `m` to `true`. It does not make
`e` optional: `flag<…>(option(…))` always sets the member, and
the generator warns. Initialize the member to `false` and put the `option`
outside:

```cpp
option(flag<&Decl::isStatic>(kw<"static">))
option(flag<&Decl::hasInit>(lit<"="> + assign<&Decl::init>(ref<Expr>)))
```

The operand of a flag can also assign members, return a called node or wrap
it with `nest`. These operations behave as they would without `flag`; the
boolean is written on the resulting node.

`nest` requires a preceding creation, adoption or write. It wraps the node
already current, which its member must be able to hold:

```cpp
ref<Name> + many(nest<&Index::array>(
    lit<"["> + assign<&Index::index>(ref<Expression>) + lit<"]">))
```

`a[0][1]` produces `Index(Index(Name(a), 0), 1)`.

For `xref<&T::m, N>`, `N` derives from one of the reference member's
admitted classes. Resolution searches only targets that are an `N`; failure
reports the expected class and name without affecting the parse.

### Nested assignments {#construction-contexts}

Each rule call, and each operand of `assign` or `append`, builds its value
separately. This separate construction is called a **construction context**.
Sequences, alternatives, repetitions and `flag` keep the current context: they
do not create a container of their own.

Thus `assign<&A::m>(assign<&B::n>(e))` builds a `B` containing `n = e` and
stores it in `A::m`. Repetition and optionality do not create separate values:

- `many(append<&A::items>(assign<&B::n>(e)))` adds one `B` per turn;
  `append<&A::items>(many(assign<&B::n>(e)))` repeatedly writes one `B`'s
  `n` and is refused.
- `assign<&A::m>(option(assign<&B::n>(e)))` always stores a `B`, empty when
  `e` is absent. Putting `option` around the outer `assign` stores nothing
  when absent.

## Expression values

The same value rules govern `assign`, `append` and
[infix operators](#operator-precedence):

| Expression | Value |
|---|---|
| An operand building a node | Its final current node; this takes precedence over text composition. |
| A [token](#tokens), rule or `section` | Its value, including its converter. |
| An alternative | The value of the alternative that matched. |
| [Conditions](#conditions) around a value | The value is unchanged. |
| A sequence of several matching elements, none building a node | An unnamed data rule: its [parts](#conversion), joined. |
| Only a quantifier, building no node | No value; [refused](refusals.md#a-directive-over-a-shape-with-no-value). |

For example, `assign<&T::name>(ref<ID> + many(lit<"."> + ref<ID>))` stores
`a.b.c` on `a . b . c`. For an optional scalar, write
`option(assign<&T::n>(ref<NUMBER>))`; to obtain text from a quantifier, name
it as a data rule. The no-value rule covers `option`, `many`, `some` and
`repeat`; it does not apply when their context builds a node.

## Conversion

A converter turns matched text into the value stored in the AST. Supply a
built-in target type, or define a function:

```cpp
static constexpr auto unquote = [](std::string_view text) {
  return text.substr(1, text.size() - 2);
};
struct NUMBER : terminal<some(d), double> {};
struct STRING : terminal<through(lit<"\"">, lit<"\"">), text_conv<unquote>> {};
```

`NUMBER` converts `12` to a `double` with value `12`. `STRING` converts
`"hello"` to a view containing `hello` by removing the two quote delimiters.

| Converter | Meaning |
|---|---|
| A type `V` | `cursiv::default_converter<V>`; built in for `string_view`, `boolean`, sized integers, `float32`/`float64` and enumerations by enumerator name. Specialize it for other types. |
| `text_conv<f>` | `f(text)`, `f(text, mem)`, or `f()` for a constant. |
| `parts_conv<f>` | `f(parts)` or `f(parts, mem)`. |
| `value<v>` | The constant `v`. |

??? note "Text parts passed to a converter"

    A terminal has one part: its raw text. A data rule's parts are its token
    occurrences and data-rule calls, in order; sequences, choices and repetitions
    add no boundaries. Trivia, implied tokens, `indent` and `dedent` give no
    parts.

    - A token contributes its spelling, converted only when its converter yields
      text. A non-text converter, such as `NUMBER`'s, is not run for a part.
    - A data-rule call contributes one part: its joined text, converted when its
      converter yields text.

    The rule's text is its parts joined. On `a.b.c`,
    `ref<ID> + many(lit<"."> + ref<ID>)` has the parts
    `{"a", ".", "b", ".", "c"}`; factoring `lit<"."> + ref<ID>` into a data rule
    `Tail` gives `{"a", ".b", ".c"}`. Both texts are `a.b.c`.

A converter runs once per converted occurrence in the kept tree, never in
conditions. `ConversionResult<V>::ok(v)` returns a value; `::err("message")`
reports a diagnostic over the matched source without selecting another parse.
Under `assign`, the member keeps its initializer; under `append`, nothing is
added. An erroneous part also puts the parent's value in error.

Allocate new text through `copy_into(mem, text)` or `text_room(mem, n)`.
This memory lives as long as the `ParseResult`. For a lambda used from a C++
module interface, give it a name before passing it to a converter.

## Operator precedence

Use `infix` for binary expressions with precedence and associativity. List
the strongest operators first, separated from weaker levels by `>>`:

```cpp
struct Expression
    : infix<Primary, &Binary::left, &Binary::op, &Binary::right,
            (left(lit<"*"> | lit<"/">) >>
             left(lit<"+"> | lit<"-">) >>
             nonassoc(lit<"<"> | lit<">">))> {};
```

Levels run from tightest to loosest; alternatives in one level share its
precedence. `left` groups `(a op b) op c`, and `right` groups
`a op (b op c)`. `nonassoc` accepts one operator but reports a chain as a
document error at the second operator; the grammar compiles. Parenthesized
chains work only if `Primary` accepts parentheses: `infix` adds none.

| Input | Grouping with the table above |
|---|---|
| `a + b * c` | `a + (b * c)` |
| `a - b - c` | `(a - b) - c` |
| `a < b < c` | A diagnostic at the second `<`; use parentheses if the language permits it. |

`Primary` must [build a node](#building-the-tree). Each operator builds the
class declaring `&L`; a lone primary returns its original node. Operators
accept literals, terminals, data rules and their sequences, possibly guarded
by conditions; [AST-producing operators are refused](refusals.md#infixoperatornottext).
Their [expression value](#expression-values) is stored in the operator
member.

Thus `left(ref<EQ> + &lit<"(">)` keeps `EQ`'s converted value, while
`left(kw<"NOT"> + kw<"EQUAL">)` gives `NOTEQUAL`, or the enumerator
`Op::NOTEQUAL` when the member is an enumeration. To give it another value,
name the operator:

```cpp
struct NotEqual : rule<kw<"NOT"> + kw<"EQUAL">, value<Op::not_equal>> {};
```

Operators sharing first tokens are distinguished by what follows.
Indistinguishable operators on different levels are
[refused](refusals.md#precedenceclash). Trivia may occur inside an operator;
`gap::none` requires adjacency, as in `lit<">"> + gap::none + lit<">">`,
which reads `>>` but not `> >`. Parentheses and ordinary calls to expression
rules start fresh expressions: `fn => a*a` reads `fn => (a*a)`. A table holds
at most 255 levels.

## Tokens

At each source position, the lexer considers the terminals expected by any
viable parse, plus active trivia. All viable parses share this selection;
only later tokens inside a [condition](#conditions) are selected separately.
The first terminals mentioned by conditions can affect the selected length,
but not a token's priority. Selection follows these rules:

1. An expected keyword beats trivia beginning with the same word, even
   longer trivia. A keyword mentioned only by a condition does not stop
   trivia.
2. The longest match wins.
3. At equal length, significant tokens beat trivia, and literals or keywords
   beat patterns. Conditions determine which branches match; they do not
   change a successful branch's category.
4. Remaining significant ties all reach the parser, a keyword and a literal
   of one spelling included. Each candidate parse continues with its own token, and
   [parse selection](#choosing-between-parses) chooses among complete parses by
   the grammar's order alone: `(lit<"if"> + kw<"then">) | (kw<"if"> + lit<"then">)`
   chooses its first alternative on `if then`. A failed continuation does
   not discard other candidates.
5. Remaining trivia ties are refused unless both are ignored.

??? note "Token categories in compound terminals"

    A token's **category** comes from the branch that reads it: a successful
    branch consuming **one keyword** is a keyword, **one literal** a literal, and
    anything else a pattern, regardless of surrounding conditions. Failed
    branches contribute nothing. If several branches of the same terminal match
    the same bytes, the match is a keyword if any of them is a keyword, otherwise
    a literal if any is a literal, otherwise a pattern. Thus
    `terminal<ref<NAME> | kw<"if">>` is a keyword on `if`, a pattern on `x`;
    `terminal<lit<"i"> + lit<"f">>` is a pattern. In
    `terminal<lit<"if"> | (kw<"if"> + &lit<";">)>`, `if;` is a keyword and `if:`
    a literal. Categories serve the lexer only: keywords and literals tie with
    each other and beat patterns (rule 3), and keyword status additionally
    enables the exception in rule 1.

Two patterns matching the same text tie as well. With
`LOW : terminal<some(cr<"a-z">)>` and `NAME : terminal<some(cr<"a-zA-Z">)>`,
`(ref<LOW> + lit<";">) | (ref<NAME> + lit<":">)` accepts `abc:` through its
second alternative, the only one that completes the input. If both alternatives
represent the same token type, define one terminal and use it in both.

A selected token cannot be split. Parser failure does not restore discarded
shorter or lower-priority tokens.
Where nothing matches, unrecognized input is an error that still ends the gap
before it, where an [implied token](#implied-tokens) may be inserted.

## Conditions

Conditions check the input without consuming it or creating AST nodes.
`&e` requires a match; `!e` requires no match. Use them when the next token
helps decide whether an expression is allowed.

For example, `!lit<"let"> + ref<ID>` accepts `letter` but rejects `let`.
A condition must hold where it appears; to choose between alternatives, use
`a | b`.

With an alphabetic `ID`:

| Condition | Holds on | Fails on |
|---|---|---|
| `!lit<"let">` before `ref<ID>` | `letter` | `let` |
| `&(ref<ID> + lit<"b">)` | `a b` | `ab` |

Rule conditions accept token expressions, `eof`, terminals, non-recursive
data rules, sequences, choices, quantifiers, groups, nested conditions, gaps
and `skip`/`tight`. AST or recursive data rules, directives, `section`,
`implied`, `indent` and `dedent` are refused. An unbounded repetition in a
condition produces a warning.

In a terminal or literal guard, conditions read adjacent bytes. Thus
`lit<"a", &lit<"b">>` requires an adjacent `b`; in a rule,
`lit<"a"> + &lit<"b">` tests the next token. `terminal<&lit<"a"> + ref<ID>>`
reads identifiers starting with `a`. A condition's match need not reach the
end; add `eof` to require it. In a rule, `!any` is not an end test: it holds
before tokens longer than one character, as `!byte` does before tokens longer
than one byte.

??? note "Exact token boundaries and nested conditions"

    With an alphabetic `ID`, `&(ref<ID> + lit<"b">)` fails on `ab` and holds on
    `a b`, and `!lit<"let"> + ref<ID>` refuses `let` but reads `letter`.
    `!lit<"<"> + lit<"<<">` reads `<<`, whereas `!lit<"=="> + lit<"=">` refuses
    `==`: the condition makes the longer boundary. `&ref<ID>` holds before
    keyword `if` even when consuming `ID` there fails; `&kw<"ARGV"> + ref<ID>`
    reads `ARGV` as an `ID`.

    In a rule:

    - **Start:** ordinary trivia are skipped; a keyword mentioned only by a
      condition does not stop them.
    - **Boundary:** the current token is chosen as in [Tokens](#tokens), from
      consuming terminals and the first terminals of all conditions standing
      there. Condition candidates affect length, even if their condition fails,
      but never displace a consuming token of equal length.
    - **First match:** the first token `e` reads, if any, must match the current
      token's whole text, regardless of the chosen terminal's identity. Named and
      inline terminals behave alike; guards may inspect text beyond that token.
    - **Continuation:** later tokens are selected by the condition's own lexer,
      under the [Tokens](#tokens) rules and the active trivia.

    `!!e` and `&(&e)` mean `&e`; `!&e` and `&!e` mean `!e`. `!(e + !f)` holds
    when every end of `e` is followed by `f`, including when `e` has no match. To
    choose between parses, use `a | b`; conditions only recognize text.

### Committed items

Use `commit(prefix, body)` when a matching prefix requires the rest of an
optional or repeated item. For example, once a comma starts another list
item, a missing item must be an error:

```cpp
ref<Item> + many(commit(lit<",">, ref<Item>))
```

This accepts `a,b` but rejects `a,` when `Item` is an identifier. Similarly,
`option(commit(kw<"else">, ref<Statement>))` requires a statement if `else`
is present.

`commit` matches `prefix + body`. It is allowed only as the direct item of
`option`, `many`, `some` or `repeat`, in a rule or rule condition. It is not
allowed inside a terminal. The prefix must consume input and use expressions
allowed in a condition.

??? note "Exact commitment and token selection rules"

    Before Max, once the minimum is met, the quantifier may stop only if its
    **prefix probe fails**:

    - The probe's first token must read the token in hand and be one the
      consuming rule can take there, after [lexical selection](#tokens).
    - The rest of the prefix reads as a [condition](#conditions).
    - In a separated list, the first probe is `prefix`; later probes are
      `sep + prefix`. Only the first token of the whole probe is checked in the
      consuming context.

    The prefix accepts condition operands and must consume input.

    Thus `many(commit(a, b)) + a + c` refuses `a c`: the prefix opens an item
    whose body is missing. Commitment affects only that quantifier: an outer
    choice can take another alternative, and an option outside the quantifier can remain
    absent.

    Token categories matter: `many(commit(ref<ID>, lit<":"> + ref<Value>)) + kw<"end">`
    reads `x:1 end`, because the keyword makes `ID` unavailable on `end`; a
    handwritten `!ref<ID>` tests text only and would refuse that `end`. The token
    in hand is chosen as in [Tokens](#tokens), so
    `many(commit(lit<"<">, lit<"x">)) + lit<"<<">` reads `<<`.

    With separators,
    `many(commit(ref<ID>, lit<":"> + ref<Value>), lit<",">) + lit<","> + kw<"end">`
    refuses `x:1,end`: the token checked in context is the comma, and past it
    `ID` reads as in a condition, where `end` spells an `ID`.

## Layout

### Gap conditions

The **gap** contains bytes between the last significant positive-width token
and the token in hand. Initially it starts at the input or section start;
finally it ends at the input or section end. Trivia, implied tokens,
`indent`, and `dedent` do not move its start.

| Condition | Holds when |
|---|---|
| `gap::has(e)` | a run starting at a character boundary matches `e` |
| `gap::is(e)` | the whole gap matches `e` |
| `gap::line` | the gap contains a line ending |
| `gap::none` | the gap is empty; even a comment without blanks makes it false |

`e` stays within the gap. It admits classes, literals, `any`, `byte`, `eol`,
sequences, choices, repetitions, negation of one class/literal/keyword, and
nonrecursive, noncapturing terminals. Other shapes are
[refused](refusals.md#gapshape). Thus
`kw<"return"> + option(!gap::line + ref<Expression>)` takes no operand from
the next line, and `ref<Operand> + gap::none + lit<"++">` requires adjacency.

### Implied tokens

`implied(tok, cond)` reads `tok` normally, or inserts it where the parser has
no normal continuation for the token in hand and `cond` holds. **Any tied
normal continuation blocks insertion**, even if it fails later: with a
statement-ending `implied(lit<";">, gap::line | eof)`, `a \n (b)` stays a
call.

`tok` must be an unguarded literal or a terminal whose body is one. Its
insertion retains its spelling but has zero width. `cond` combines gap
conditions, `&e`, `!e`, and `eof` with `|` and `+`; its terminals participate
in lexing wherever insertion is possible, under [Conditions](#conditions).

```cpp
implied(lit<";">, gap::line | &lit<"}"> | eof)
```

### Trivia scopes

`skip(ignored<Ts…>, hide<Ts…>, e)` replaces trivia **between `e`'s tokens**:
ignored terminals are dropped, hidden ones kept. `tight(e)` is
`skip(ignored<>, hide<>, e)`. Called rules inherit the scope; trivia before
and after `e` belongs to the caller. Thus
`tight(lit<"["> + ref<ID> + lit<"]">)` refuses `[ name ]`. A caller's token
may be trivia inside and become a token again outside. Readings assigning
different scopes to one point are [refused](refusals.md#regionboundaryshared).

### Indentation

`indent` and `dedent` stand bare in rules or as a terminal's whole body. They
compare content lines' leading space/tab runs as **exact strings**, without
tab expansion:

- Equal runs keep the level; a proper extension opens a level.
- An enclosing level's run closes every level above it, producing one
  `dedent` each.
- Any other run, or form feed in a run, is an indentation error.
- EOF closes every level.

`using indent_newline = NEWLINE;` names the statement-ending terminal. Blank
and comment-only lines do not change levels provided `NEWLINE` reads them.
Trivia consuming a backslash together with a line ending joins lines.

`indent_open` and `indent_close` identify brackets within which indentation
is suppressed and `NEWLINE` is blank. Each may name a spelling, choice of
spellings, or terminal. A spelling marks every terminal whose whole body is
exactly that literal. A terminal read as trivia never acts as a bracket.

```cpp
struct WS : terminal<some(cr<R"( \t)">)> {};
struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
struct NEWLINE
    : terminal<eol +
               many(many(cr<R"( \t)">) + option(ref<COMMENT>) + eol) +
               option(many(cr<R"( \t)">) + option(ref<COMMENT>) + eof)> {};
using indent_newline = NEWLINE;
static constexpr auto indent_open = lit<"("> | lit<"["> | lit<"{">;
static constexpr auto indent_close = lit<")"> | lit<"]"> | lit<"}">;
```

`NEWLINE` absorbs the blank and comment-only lines after a line ending, a
final one without line ending included; whitespace and comments stop short of
`eol`, and the absorbed bytes belong to the `NEWLINE` token.

## Advanced rules

### Character class details

`cr` uses regex bracket syntax without brackets. `a-z` is a range; `-` is
literal first, last, or immediately after a range. Leading `^` complements;
elsewhere it is literal. Escapes are `\n`, `\r`, `\t`, `\f`, `\v`, `\b`
(backspace), `\0` (NUL when no digit follows), and `\cX` (control letter).
Escaped punctuation is itself. `\xHH`, `\uHHHH`, and `\u{H…}` denote
scalars, never raw bytes. Use raw C++ strings: `cr<R"(\s)">`.

Unknown escapes, reversed ranges, class range endpoints (`a-\d`), explicit
surrogates, and values past U+10FFFF are compile-time refusals. Ranges
between scalar endpoints skip surrogates.

`\d` is `0-9`, `\w` is `a-zA-Z0-9_`, and `\s` is space, tab, LF, CR, FF, VT.
`\D`, `\W`, `\S` complement them; Unicode properties are separate.

Class `|` forms one union class. `~` complements over **all characters**,
and double complement restores the class. Unicode properties contain only
scalars; their complements also contain bytes starting no scalar. Thus
`cr<"é">` refuses a lone byte `\xE9`, which `~cr<"é">` reads. For strictly
valid UTF-8, `cr<R"(\u{0}-\u{10FFFF})">` reads one scalar. Write `~(a | b)`
to complement a union. There is no normalization or grapheme grouping.

### Captures

`mark<Tag>(run)` remembers exact bytes; `run` is a class or one-character
literal, optionally under `many`, `some`, or `repeat`. `same<Tag>` reads the
stored bytes:

```cpp
struct Eq {};
through(lit<"["> + mark<Eq>(many(lit<"=">)) + lit<"[">,
        lit<"]"> + same<Eq> + lit<"]">)
```

This reads `[[x]]` or `[=[x]=]` with matching delimiter runs. Each token and
each terminal call starts a fresh capture scope: captures cross no call in
either direction. Lookaheads see current captures, but their writes do not
escape. A later mark replaces the value. Every incoming path in the same
scope must mark a tag before `same` ([`echoUnmarked`](refusals.md#echounmarked)).

### Recursive terminals

Recursion is allowed only when every cycle consumes before calling again
([`terminalCycle`](refusals.md#terminalcycle)); calling a parser rule is
refused ([`terminalReadsRule`](refusals.md#terminalreadsrule)). In recursive
delimited text, exclude both delimiters from the character fallback, so it
neither swallows an unfinished opener nor runs past a closer:

```cpp
struct COMMENT
    : terminal<lit<"/*"> +
               many(ref<COMMENT> | (!lit<"/*"> + !lit<"*/"> + any)) +
               lit<"*/">> {};
```

### Left recursion

A left-recursive rule calls itself before consuming a token. For example,
`Expression := Expression "!" | Name` applies one or more suffix operators
to a name, or returns the name alone. Both engines support direct left
recursion and supported indirect cycles.

Use [infix](#operator-precedence) for binary operators. If a larger recursive
cycle fails to generate with LL, see the
[LL restrictions](refusals.md#leftrecursion).

??? note "Exact ordering of recursive alternatives"

    The [parse selection rules](#choosing-between-parses) treat left recursion as
    **a base followed by turns**. The base is decided first, among the other alternatives in written order,
    wherever the recursive alternatives stand; then another turn wins over stopping, and
    turns follow written order. For example:

    ```text
    S := S x | a x | a          means (a x | a) many(x)
    E := E "!" | "-" E | a      means ("-" E | a) many("!")
    ```

    `S` reads `a x` as its base `a x`. Both the displayed `E` and
    `E := "-" E | E "!" | a` read `-a!` as `-(a!)`. For `(-a)!`, separate levels:
    `E := E "!" | N`, `N := "-" N | a`. This loop only orders parses: each
    turn retains its rule call, node, writes and conversion.

    **Indirect recursion.** The initially invoked rule is called the **head**.
    Leading calls retain that head; a call after a consumed token starts its own
    parse and head, even inside the same recursion. Other callers do not change
    this head or its base and turn ordering.

    Walk the head's alternatives in written order. A leading call to another rule of the
    recursion descends into its alternatives in place. Returning to a rule already on the
    active path forms a turn of **that rule's loop**, kept where that rule
    stands; other alternatives are bases. Bases and turns are ordered as the walk meets
    them, under the same parse selection rules:

    ```text
    A := B "x" | "a" "x"       B := A "y" | "a"
    Call A on a x y x: A → B x → A y x → B x y x → a x y x
    Call B on a x y:   B → A y → a x y
    ```

    An [`infix`](#operator-precedence) in such a recursion counts as its levels,
    one rule per level (`E_k := E_k op E_{k-1} | E_{k-1}` for `left`, mirrored for
    `right`), with the primary as level 0.

    The LL engine needs, in an indirect left recursion, an alternative consisting solely
    of a call to the next rule of the cycle, and it refuses some competing nested
    loops; both limits are reported as [`leftRecursion`](refusals.md#leftrecursion),
    which states them exactly.

**Refused forms:**

- An alternative beginning and ending with its own rule (`E "+" E`, `E E`,
  `E "?" E ":" E`) must declare its nesting: bare forms are
  [refused](refusals.md#selfboundedarm). Use
  [`infix`](#operator-precedence) or levels such as `E := E "+" T | T`,
  `T := T "*" F | F`.
- A cycle with no input progress, such as `A := A | x`, is
  [refused](refusals.md#unitproductioncycle).
- Left recursion behind a nullable item, such as `A := B A x | y` with
  nullable `B`, is [unsupported](refusals.md#hiddenleftrecursion). Rewriting
  it may change the language.

### Embedded grammars

`section<G>(open, close)` reads caller token `open`, then `G.entry` as a
whole document, then caller token `close`. Its value is the entry's value:
`assign` and `append` follow the [expression value rules](#expression-values);
a bare section with an AST entry [adopts its node](#adoption-and-classes). In
a data rule it supplies three parts: `open`, the entry text as a data-rule
part, and `close`; the containing data rule's text therefore includes the
delimiters ([Conversion](#conversion)).

```cpp
assign<&Cell::formula>(section<calc::Formula>(lit<"<expr>">, lit<"</expr>">))
```

`G` owns interior trivia up to `close`, keywords, `word_boundary`, and
overrides. **The host's trivia policy does not run just before `close`.** The section
ends at the first token boundary where `close` matches, never inside a guest
token. EOF before it is a missing-close error. `G` sees only the section: its
`eof` holds at the closing boundary, and conditions cannot see past it. The
tree retains both languages' nodes; dumps qualify guest rules as `G::Rule`.

With `[` and `]` delimiters on `[a]`, guest `lit<"a"> + &eof` succeeds;
`lit<"a"> + &lit<"]">` does not.

Refused: a guest token matching or beginning with `close`
([`sectionEndShared`](refusals.md#sectionendshared)); a point read both
inside and outside ([`sectionPointShared`](refusals.md#sectionpointshared));
indentation on either side ([`sectionIndented`](refusals.md#sectionindented));
a terminal entry for `G`.

### Derived grammars

A `cursiv::extends<Base>` grammar may add rules, redeclare `entry`, `ignore`,
`hidden`, or `word_boundary`, and override rules:

```cpp
struct G : cursiv::extends<Base> {
  template <class> struct override;
  template <class T>
    requires std::same_as<T, Base::WS>
  struct override<T> : terminal<some(s)> {};
};
```

Every call to an overridden rule uses its replacement body. It preserves the rule's terminal/rule kind,
AST/data category, and class family, and inherits its converter unless restated. `super`
splices in the replaced body: `ref<Extra> | super` adds an alternative. With
`extends<A, B>`, anything declared or overridden by both parents must be
restated. A grammar used in a [section](#embedded-grammars) keeps its own
rules. See the [derived grammar recipes](../recipes/derived-grammars.md).

## Where expressions are allowed {#expression-contexts}

| Element | Terminal | Rule | Rule condition |
|---|---|---|---|
| `lit`, `kw`, `ikw`, classes, `any`, `byte`, `eol` | Adjacent input | One token | Allowed |
| `eof` | Test | Test | Allowed |
| `ref<T>`, terminal `T` | Inline body | Token `T` | Text of `T`, under [Conditions](#conditions) |
| `ref<D>`, data rule `D` | Refused | Value of `D` | Allowed unless recursive |
| `ref<R>`, AST rule `R` | Refused | Node of `R` | Refused |
| `+`, `\|`, `option`, `many`, `some`, `repeat` | Allowed | Allowed | Allowed |
| `commit(p, b)` | Refused | Direct quantifier item | Direct quantifier item |
| Unordered groups, `skip`, `tight`, gap conditions | Refused | Allowed | Allowed |
| `through` | Allowed | One anonymous token | One token |
| `mark`, `same` | Allowed | Inside `through` or an inline terminal | Same restriction |
| `&e`, `!e` | Test adjacent input | [Conditions](#conditions) | Allowed |
| `implied`, `indent`, `dedent` | Only `indent`/`dedent`, as the whole body | Allowed | Refused |
| Directives, `section<G>(…)` | Refused | Allowed | Refused |

`eof`, conditions, `create`, absent options and implied tokens consume
nothing. `indent` and `dedent` have no width but each counts as a consumed
token, so a loop reading one makes progress.

## Warnings and limits

The generator warns, naming the expression, about a `flag` whose operand may
read nothing, a repetition nested in a repetition, and an unbounded
repetition in a condition. Warnings never rewrite the grammar.

A form described as **refused**, including a support limit, fails grammar
compilation; see [Grammar errors and fixes](refusals.md). It never silently changes the
language or tree. Rejected input, such as invalid indentation or a chain at
a `nonassoc` level, is an error in the document.

**C++ precedence**, tightest first: unary `!`, `&`, `~`; `+`; `>>`; binary
`&`; `|`. Thus `&a + b` means `(&a) + b`; use `&(a + b)` to test both.
Parenthesize mixed binary operators anyway (`(a + b) & c`): compilers warn
on them.
Parenthesize `infix` levels: an unparenthesized `>>` inside `<…>` would close
the template.
