# Grammar reference

Declare a grammar as a C++ type derived from `cursiv::GrammarSpec`: terminals
recognize text, rules combine tokens, and directives fill your AST. To learn
step by step, start with [Write a grammar](../learn/write-a-grammar.md).

The syntax applies to both engines, LR (the default and recommended one) and
LL, within their [support limits](refusals.md#where-the-engines-differ).
Snippets assume the surrounding grammar and AST declarations. Examples show
what the grammar accepts or rejects before error recovery repairs the input.

| To… | See |
|---|---|
| Match identifiers, numbers or comments | [Text](#text) |
| Know how text is cut into tokens | [Tokens](#tokens) |
| Combine rules or repeat an item | [Composition](#composition) |
| Fill AST members or wrap an expression | [Building the tree](#building-the-tree) |
| Convert text to numbers or other values | [Conversion](#conversion) |
| Define operators and their precedence | [Operator precedence](#operator-precedence) |
| Share parts between rules | [Reuse grammar pieces](#reuse-grammar-pieces) |
| Control whitespace or indentation | [Layout](#layout) |
| Understand an error from the generator | [Grammar errors](refusals.md) |

## Quick start

```cpp
#include <cstdint>
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace ast {
struct Variable : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  int64 value = 0;
};

struct Module : cursiv::Extends<cursiv::AstNode> {
  vector<Variable*> variables;
};
}  // namespace ast

struct Variables : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"//">, &eol | eof)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct INTEGER : terminal<as<std::int64_t>(some(d))> {};

  using ignored = WS;
  using hidden = COMMENT;

  struct Variable
      : rule<kw<"let"> + assign<&ast::Variable::name>(ref<ID>) + lit<"=">
             + assign<&ast::Variable::value>(ref<INTEGER>) + lit<";">> {};
  struct Module : rule<many(append<&ast::Module::variables>(ref<Variable>))> {};
  using entry = Module;
};
```

On `let x = 1; let y = 2;`, this builds a `Module` holding two `Variable`
nodes; on empty text, a `Module` with no variables. `assign` stores one value,
`append` adds one element, and the members they name determine each rule's
AST class.

Recommended naming: AST classes in an `ast` namespace, so that each rule can
take its class's name without a `Rule` suffix; terminals in UPPER_CASE;
fragments in snake_case. For a complete project, follow
[Build a language end to end](../learn/walkthrough.md).

## Rules and grammar settings {#model}

A **terminal** recognizes one token, such as an identifier or number. A
**rule** combines tokens and may build an AST node. Call either with `ref<R>`.

| Declaration | Meaning |
|---|---|
| `terminal<Body>` | One token, read from adjacent input with no trivia inside; its value is its text. |
| `rule<Body>` | Tokens, with trivia allowed between them. |
| `terminal<as<X>(Body)>`, `rule<as<X>(Body)>` | A terminal or data rule whose text is [converted](#conversion). |
| `ref<R>` | A call to a terminal or rule. `ref<terminal<…>>` and `ref<rule<…>>` declare one in place. |
| `infix<Primary, T, Levels>` | Operator expressions with [precedence](#operator-precedence). |
| `static constexpr auto f = e;` | A **fragment**: `e` written in place wherever `f` is used; see [Reuse grammar pieces](#reuse-grammar-pieces). |

An **AST rule** builds or returns one node. A **data rule** returns a value
instead: a lone reference passes the value of the rule it calls, an
alternative passes the value of the branch it took (all branches give one
type), and anything else gives the text of its tokens, joined without
whitespace or comments:

```cpp
struct QualifiedName : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
struct Visibility : rule<as<Vis::Public>(kw<"public">)
                         | as<Vis::Private>(kw<"private">)> {};
```

`a . b` gives `a.b`, and `private` gives `Vis::Private`.

| Setting | Meaning |
|---|---|
| `using entry = R;` | Read a whole document from `R`. `R` builds a node, except that a [section](#embedded-grammars)'s entry may be a data rule. |
| `using ignored = T;` or `list<T…>` | Trivia dropped from the CST. |
| `using hidden = T;` or `list<T…>` | Trivia kept in the CST, so comments can attach to what follows. |
| `static constexpr auto word_boundary = c;` | Class continuing a word; `w` by default. |
| `indent_open`, `indent_close`, `indent_newline` | [Indentation](#indentation) settings. |

Whitespace and comments are called **trivia**. A trivia terminal must consume
input, and belongs to `ignored` or `hidden`, never both.

## Text

### Characters

A character class matches one character: one Unicode scalar in valid UTF-8,
or one invalid byte. [Character class details](#character-class-details)
covers escapes and Unicode.

| Spelling | Reads |
|---|---|
| `cr<"a-zé_">` | one member of the class; `cr` stands for *character range* |
| `cr<"^…">`, `~c` | one character outside the class |
| `uc::L`, `uc::Lu`, `uc::Nd`, … | a Unicode general category or subcategory |
| `uc::XID_Start`, `uc::XID_Continue`, `uc::White_Space` | a Unicode property |
| `s`, `w`, `d`; `S`, `W`, `D` | ASCII shorthands and their complements |
| `any`; `byte` | one character; one raw byte |

### Literals and keywords

| Spelling | Reads |
|---|---|
| `lit<"…">` | these exact bytes, never empty; for punctuation, such as `lit<";">` |
| `lit<"…", G>` | the literal if `G` holds right after its bytes: `lit<"=", !lit<"=">>` |
| `kw<"…">` | the literal unless the next character continues a word; for words, such as `kw<"if">` |
| `ikw<"…">` | the same, ignoring ASCII letter case: `ikw<"if">` also matches `IF` |
| `eol` | CRLF, LF or CR; not end of input, so write `eol | eof` when either is allowed |
| `eof` | end of input, after any trailing trivia |

`kw<"if">` matches `if` but not the start of `iffy`, whereas `lit<"end">`
also matches `end` in `endx`; the generator
[warns](refusals.md#a-word-literal-with-no-guard) about such a literal in a
rule unless it has a guard. The `word_boundary` setting decides which
characters continue a word: `w` (`a-zA-Z0-9_`) by default, or for example
`uc::XID_Continue | cr<"_$">` in a Unicode language. `kw<"if">.doc<"…">()`
adds hover text.

Keywords are contextual: a word reads as a keyword only where the grammar
expects that keyword ([Tokens](#tokens)). `type` reads as an `ID` where only an
identifier is expected, and as the keyword where `kw<"type">` is expected too;
write `ref<ID> | kw<"type">` to accept it as a name there. To reserve words,
keep the grammar permissive and report identifiers spelled like reserved
words from a [validation check](../recipes/validation.md).

### Terminals

Inside a terminal, a call to another terminal reads its text as part of the
same token: `terminal<ref<ID> + lit<":"> + ref<ID>>` reads `a:b` as one token,
whereas a rule with the same body reads three tokens and allows trivia
between them. A terminal used as a token or as trivia must consume input,
unless its whole body is `indent` or `dedent`; one used only in a condition
may match nothing. See also [Recursive terminals](#recursive-terminals).

### Through

`through(open, close)` reads delimited text. It means
`open + many(!close + any) + close`, so it stops at the first `close`:

```cpp
struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
```

On `/* a */ b */`, the token ends at the first `*/`. Write `close | eof` to
accept an unfinished delimiter: `through(lit<"//">, &eol | eof)` stops before
the line ending or at the end of input. In a rule, `through` reads one token.

## Tokens

The lexer cuts the text into tokens as the parser reads it. A parse is
**viable** when it has read the text so far without error. A token's
**category** is keyword, literal or pattern: a terminal that reads one keyword
or one literal has that category, and any other, such as `some(d)`, is a
pattern.

| Rule | Example |
|---|---|
| Only the terminals that a viable parse expects here are tried, plus the active trivia. | Where only `ID` is expected, `if` reads as an `ID`, even if the grammar uses `kw<"if">` elsewhere. |
| The longest match wins. At equal length, significant tokens beat trivia, and keywords and literals beat patterns. An expected keyword also beats trivia that begins with the same word. | Where `kw<"if">` and `ID` are both expected, `if` reads as the keyword and `iffy` as an `ID`. |
| Other ties all reach the parser, and each parse continues with its own token. | With `LOW : terminal<some(cr<"a-z">)>` and `NAME : terminal<some(cr<"a-zA-Z">)>`, `(ref<LOW> + lit<";">) | (ref<NAME> + lit<":">)` accepts `abc:`. |
| A chosen token is never cut again, even if the parse then fails. | `(lit<"a"> + lit<"b">) | (lit<"ab"> + lit<"c">)` rejects `ab`. |

Two trivia terminals that tie are [refused](refusals.md#triviatied) unless
both are ignored. If two terminals stand for the same kind of token, define
one and use it everywhere.

## Composition

`a + b` reads `a`, then `b`; `a | b` reads either alternative.

```cpp
kw<"let"> + ref<ID>                         // a keyword followed by a name
kw<"public"> | kw<"private">                // either keyword
many(append<&Block::statements>(ref<Statement>)) // zero or more statements
```

!!! note "C++ spelling"

    A grammar is C++ code, so C++ rules apply:

    - Parenthesize mixed operators: `(a + b) | c`. GCC warns about `a + b | c`.
    - Define a rule above the rules that call it, or forward-declare it with
      `struct Expr;`. A rule can call itself without one.
    - Join a condition with `+`: `a + &b`. C++ reads `a &&b` as `a && b`.
    - Unary `!`, `&` and `~` bind tightest, then `+`, then `|`: `&a + b`
      means `(&a) + b`; write `&(a + b)` to test both.

### Repetition

| Form | Count |
|---|---|
| `option(e)`, `many(e)`, `some(e)` | Zero or one, zero or more, one or more |
| `many(e, sep)`, `some(e, sep)` | Zero or more, one or more, with separators between items |
| `repeat<N>(e)` | Exactly N, with N ≥ 1 |
| `repeat<Min, Max>(e)` | Min to Max; Min ≤ Max and Max ≥ 1; `kUnbounded` means no upper bound |

Put `append` inside the repetition to collect one child per item, as in
`many(append<&Block::statements>(ref<Statement>))`; `append<…>(many(…))` is
refused, because `append` takes one element. A trailing separator is
explicit: `option(some(e, sep) + option(sep))`. In a rule or a recursive
terminal, a repeated item must consume input: write `many(x)`, not
[`many(option(x))`](refusals.md#nullablerepetition). Large `repeat` bounds
make the generated parser [grow](refusals.md#counts-a-program-cannot-hold).

### Unordered groups

`unordered(a, b, …)` reads each of its two or more items once, in any order.
An item may be optional, with `option(x)`, `many(x)` or `repeat<0, n>(x)`; any
other item must read input ([refused](refusals.md#nullableunordereditem)
otherwise).

| Expression | Accepted input |
|---|---|
| `unordered(a, b)` | `a b` or `b a` |
| `unordered(option(a), b)` | `b`, `a b` or `b a` |
| `unordered(some(a), b)` | `a a b` or `b a a`, but not `a b a` |

For items that may repeat anywhere, collect them with
`many(append<&ast::Box::settings>(ref<Setting>))` and report duplicates from a
[validation check](../recipes/validation.md). Groups have a
[size limit](refusals.md#unorderedtoowide).

### Choosing between parses

The parser keeps the alternatives that can complete the input with the
tokens the lexer chose. When several complete parses remain, it compares
their decisions from left to right, outer before nested, and the first
difference picks the parse by the grammar's order:

| Construct | Preferred decision |
|---|---|
| Choice | First written alternative |
| Option | Present |
| Repetition | Another item |
| Unordered group | Items in written order; optional items present |

A terminal takes its longest match instead, and
[left recursion](#left-recursion) has its own order. An alternative that reads
the same tokens as an earlier one is never chosen: the generator
[warns and drops it](refusals.md#twinproductions).

So with `option(kw<"else"> + ref<Statement>)` at the end of an `if` rule,
`if a if b x else y` gives the `else` to the nearest `if`;
[`commit`](#committed-items) does the same without keeping the other
reading. Factoring `A B | A C` into `A (B | C)` keeps the accepted input but
can change the tree, because `A`'s own decision then comes first.

## Building the tree

Directives store parsed values in your AST members, as in the
[quick start](#quick-start).

| Directive | Effect |
|---|---|
| `create<T>`, `create<T>(e)` | Gives the [current node](#construction-contexts) the class `T`, keeping earlier writes. `create<T>(e)` is `create<T> + e`. |
| `assign<&T::m>(e)` | Builds one value separately and stores it in the single-valued member `m`. |
| `append<&T::m>(e)` | Builds one value separately and adds it to the `vector` member `m`. |
| `flag<&T::m>(e)` | Reads `e`, then sets the `boolean` member `m` to `true`. |
| `nest<&T::m>(e)`, `nest<W, &T::m>(e)` | Wraps the current node in a new `T`, or `W` derived from `T`, holding it in `m`; `e` writes into the wrapper. |
| `assign<xref<&T::m, N>>(e)` | Assigns a reference that links only to targets that are an `N`, a class derived from one the reference admits. |

A rule builds the most derived class that declares the members it writes. A
member pointer names the class that **declares** the member, not the class
written before `::`: if `Base` declares `name`,
`kw<"derived"> + assign<&Derived::name>(ref<ID>)` builds a `Base`. Write
`create<T>` to build another class, to choose one when the members come from
unrelated classes, or to build a node from a rule that writes no member:

```cpp
create<Derived> + kw<"derived"> + assign<&Derived::name>(ref<ID>)
create<Null>(kw<"null">)
```

Every node must be returned by its rule or stored in an AST member.

### Nested assignments {#construction-contexts}

Each rule call, and each operand of `assign` or `append`, builds its value in
its own **construction context**; the node its directives write is its
**current node**. Sequences, alternatives, repetitions and `flag` stay in the
current context.

Thus `assign<&A::m>(assign<&B::n>(e))` stores in `A::m` a `B` holding
`n = e`. `assign<&A::m>(option(assign<&B::n>(e)))` always stores a `B`, empty
when `e` is absent; put the `option` outside the outer `assign` to store
nothing.

### Adoption and classes

An unassigned call to an AST rule returns that rule's node: this is
**adoption**. `lit<"("> + ref<Expression> + lit<")">` returns the
`Expression` node, with no wrapper for the parentheses; `assign` or `append`
around the call stores the node in a member instead.

A **path** is one way through a rule's body: one alternative at each choice
and one number of turns at each option or repetition. On a path, an adoption
cannot follow a write, `create`, `nest` or another adoption in the same
construction context: hold the node with `assign` or `append`, or write the
shared part as a [fragment](#reuse-grammar-pieces). After an adoption, writes
and `nest` may follow, but not `create`. A rule must return a node on every
path, so `option(ref<Expression>)` alone is refused.

### Writes and wrapping

A single-valued member is written **at most once per node and path**,
including by an adopted rule: `many(assign<&T::m>(…))` is refused unless a
`nest` makes a new node per turn. Directives inside a
[condition](#conditions), a `through` or a `mark` are refused, because they
would never run.

`flag<&T::m>(e)` does not make `e` optional: `flag<…>(option(…))` always sets
the member, and the generator warns. Initialize the member to `false` and put
the `option` outside: `option(flag<&Decl::isStatic>(kw<"static">))`.

`nest` wraps the node already current, so it needs a preceding creation,
adoption or write, and its member must be able to hold that node:

```cpp
ref<Name> + many(nest<&Index::array>(
    lit<"["> + assign<&Index::index>(ref<Expression>) + lit<"]">))
```

`a[0][1]` produces `Index(Index(Name(a), 0), 1)`. Such a rule returns the most
derived common base of its possible results, here of `Name` and `Index`.

## Expression values

The value of an operand of `assign`, `append` or an
[infix operator](#operator-precedence):

| Expression | Value |
|---|---|
| An operand building a node | Its final current node. |
| A [token](#tokens), rule or `section` | Its value, after its converter. |
| An alternative | The value of the alternative that matched. |
| A sequence building no node | Its text, joined as in a [data rule](#model). |
| Only a quantifier, building no node | No value; [refused](refusals.md#a-directive-over-a-shape-with-no-value). |

Thus `assign<&T::name>(ref<ID> + many(lit<"."> + ref<ID>))` stores `a.b.c`,
but `assign<&T::n>(option(ref<NUMBER>))` is refused: write
`option(assign<&T::n>(ref<NUMBER>))`, or name the quantifier as a data rule
to store its text.

## Conversion

`as<X>(e)` gives a value to what `e` reads. Around the whole body of a
terminal or data rule, it converts the rule's text:

```cpp
static constexpr auto unquote = [](std::string_view text) {
  return text.substr(1, text.size() - 2);
};
struct NUMBER : terminal<as<double>(some(d))> {};
struct STRING : terminal<as<unquote>(through(lit<"\"">, lit<"\"">))> {};
```

`NUMBER` converts `12` to the `double` 12, and `STRING` converts `"hello"` to
`hello`.

| `X` | Value |
|---|---|
| A type `T` | `cursiv::default_converter<T>`, built in for `string_view`, `boolean`, sized integers, `float32`/`float64` and enumerations by enumerator name. For other types, specialize it with `static ConversionResult<T> convert(std::string_view text)`, which may also take `mem`. |
| A function `f` | `f(text)` or `f(parts)`, either with `mem` after it, or `f()`. |
| Another constant | The constant itself, such as an enumerator. |

`text` is a `std::string_view`; `parts` a `std::span<const std::string_view>`
of a data rule's tokens and data-rule calls, such as `{"a", ".", "b"}` for
`a.b`; `mem` a `std::pmr::memory_resource*`. Give each parameter its type: an
`auto` parameter, as in a generic lambda, is refused. Allocate new text with
`copy_into(mem, text)` or `text_room(mem, n)`; it lives as long as the
`ParseResult`.

A converter returns a value or a `ConversionResult`: `::ok(v)` gives a value,
and `::err("message")` reports a diagnostic over the matched text; the member
then keeps its initializer, or `append` adds nothing. Converters run only for
the selected parse, never in conditions.

`as` can also give a value to a part of a rule, as in the `Visibility` rule
[above](#model), without changing its tokens: `private` stays a keyword. `as`
cannot wrap another `as` (combine the two in one function) or an expression
that builds an AST node.

## Operator precedence

`infix<Primary, T, Levels>` reads operator expressions over `Primary`. List
the levels in `levels(…)`, from the tightest-binding to the loosest. Binary
levels are `left`, `right` or `nonassoc`; a `prefix` or `postfix` level reads
its operator before or after one operand:

```cpp
struct Expression
    : infix<Primary, ast::Binary,
            levels(postfix<ast::Postfix>(lit<"++"> | lit<"--">),
                   prefix<ast::Unary>(lit<"-"> | lit<"!">),
                   left(lit<"*"> | lit<"/">), left(lit<"+"> | lit<"-">),
                   nonassoc(lit<"<"> | lit<">">),
                   prefix<ast::Not>(kw<"not">),
                   left(kw<"and">), left(kw<"or">))> {};
```

Operators in one level share its precedence. `left` groups `(a op b) op c`,
`right` groups `a op (b op c)`, and `nonassoc` reports a chain as a document
error at the second operator. A prefix or postfix applies to everything that
binds tighter than it, so an operand cannot start with a looser prefix or end
with a looser postfix:

| Input | Grouping |
|---|---|
| `a + b * c` | `a + (b * c)` |
| `a - b - c` | `(a - b) - c` |
| `a < b < c` | A diagnostic at the second `<` |
| `-a * b`, `-a++` | `(-a) * b`, `-(a++)` |
| `not a < b and c` | `(not (a < b)) and c` |
| `a < not b` | Rejected; write `a < (not b)` |

`infix` adds no parentheses: `Primary` must accept them if the language does,
and they start a fresh expression. A single level needs no `levels`, but each
argument of `levels` must be a level; a bare operator does not
[compile](refusals.md#a-level-table-that-lists-an-operator). An operator may
be both prefix and binary, like `-` here, but not both postfix and binary
([refused](refusals.md#postfixbinaryclash)), nor on two levels of one table
([refused](refusals.md#precedenceclash)); two tables may each give it a level.

`Primary` must [build a node](#building-the-tree); a lone primary returns it.
A binary operator builds a `T` and writes its members `left`, `op` and
`right`; `prefix<U>` and `postfix<U>` build a `U` and write its `op` and
`operand`. These names apply even when a base class declares the members. To
use other members, name them after the levels, or write
`prefix<&U::op, &U::operand>(op)` and `postfix<&U::operand, &U::op>(op)`,
which build the class that declares `op`:

```cpp
struct Assignment
    : infix<Primary, ast::Assign, right(lit<"=">),
            &ast::Assign::target, &ast::Assign::op, &ast::Assign::value> {};
```

An operator is a literal, terminal, data rule or a sequence of them, possibly
with conditions; one that builds a node is
[refused](refusals.md#infixoperatornottext). Trivia may occur between its
tokens unless `gap::none` forbids it: `lit<">"> + gap::none + lit<">">` reads
`>>` but not `> >`. The operator member stores the operator's
[expression value](#expression-values): `kw<"NOT"> + kw<"EQUAL">` gives
`NOTEQUAL`, or the enumerator `Op::NOTEQUAL` when the member is an
enumeration. Use [`as<V>(e)`](#conversion) for another value, such as
`left(as<Op::mul>(lit<"*">) | as<Op::div>(lit<"/">))`.

## Reuse grammar pieces

A fragment is written in place at each use, so its directives write the
current node of the rule that uses it; calling an AST rule would build a
separate node instead. Use fragments for the parts that several rules share:

```cpp
// The prologue of every declaration.
static constexpr auto modifiers =
    many(append<&ast::Decl::annotations>(ref<Annotation>))
    + option(flag<&ast::Decl::exported>(kw<"export">));

struct Function : rule<create<ast::Function> + modifiers + kw<"fn">
                       + assign<&ast::Decl::name>(ref<ID>) + …> {};
struct Variable : rule<create<ast::Variable> + modifiers + kw<"var">
                       + assign<&ast::Decl::name>(ref<ID>) + …> {};
```

These members belong to `ast::Decl`, so each rule names its class with
`create<T>`; otherwise it would build an `ast::Decl`. One rule can also read
the prologue once and let each alternative pick the class:
`modifiers + (create<ast::Function>(fn_body) | create<ast::Variable>(var_body))`.

A template parameter makes a fragment configurable:

```cpp
template <auto Member>
static constexpr auto params =
    lit<"("> + many(append<Member>(ref<Param>), lit<",">) + lit<")">;
```

`params<&ast::Function::params>` reads a parameter list into a function, and
`params<&ast::Lambda::params>` into a lambda. A rule can be a template too;
each instantiation is a separate rule. A fragment cannot refer to itself, so
recursion needs a rule.

## Conditions

A condition checks the input without consuming it or building anything: `&e`
requires a match, `!e` requires no match. It must hold where it appears; to
choose between alternatives, use `a | b`. In a rule, it tests the text of the
next tokens; in a terminal or a literal guard, the adjacent bytes. With an
alphabetic `ID`:

| Condition | Holds on | Fails on |
|---|---|---|
| `!lit<"let">` before `ref<ID>` in a rule | `letter` | `let` |
| `!lit<"let">` in a terminal | `lex` | `letter`, whose bytes start with `let` |
| `&(ref<ID> + lit<"b">)` in a rule | `a b` | `ab` |
| `lit<"a", &lit<"b">>` | `ab` | `a b` |

A condition's match need not reach the end; add `eof` to require it, and test
the end with `eof`, not `!any`. In a rule, a condition's first token must be
the whole current token: `!lit<"<"> + lit<"<<">` reads `<<`. The generator
[warns](refusals.md#warned-not-refused) about an unbounded repetition in a
condition; bound it, as in `repeat<1, 8>(lit<"-">)`, when the language
permits. See [Where expressions are allowed](#expression-contexts) for what a
condition may contain.

### Committed items

`commit(p, b)` is an item of an `option`, `many`, `some` or `repeat` that
reads `p + b`; once the prefix `p` matches, the body `b` must follow, and a
missing body is reported right after `p`. This can change the language:

| Expression | On `x:1 y` |
|---|---|
| `many(ref<ID> + lit<":"> + ref<Value>) + ref<ID>` | Read: the repetition stops before `y`. |
| `many(commit(ref<ID>, lit<":"> + ref<Value>)) + ref<ID>` | Rejected: `y` matches the prefix, so `:` must follow. |

`many(commit(p, b))` reads like `many(p + b) + !p`, and `option(commit(p, b))`
like `(p + b) | !p`. Thus `option(commit(kw<"else">, ref<Statement>))` gives
each `else` to the nearest `if` and drops the other reading.

`commit` must be the direct item of its quantifier. The prefix must consume
input and contain only what a condition allows, so it takes no directives:
commit on a token you do not store, such as a separator or a keyword, and
read what you store in the body. In a separated list, the prefix after a
separator is tested as text, so guard it against what may follow a trailing
separator, as in `!kw<"end"> + ref<ID>`.

## Layout

### Gap conditions

The **gap** is the text, trivia included, between the current token and the
previous significant token with a width (or the start of input).

| Condition | Holds when |
|---|---|
| `gap::has(e)` | a run starting at a character boundary matches `e` |
| `gap::is(e)` | the whole gap matches `e` |
| `gap::line` | the gap contains a line ending |
| `gap::none` | the gap is empty; even a comment without blanks makes it false |

`e` is a simple text pattern: a class, a literal, `eol` or a repetition of
them ([other shapes are refused](refusals.md#gapshape)). For example,
`kw<"return"> + option(!gap::line + ref<Expression>)` takes no operand from
the next line, and `ref<Operand> + gap::none + lit<"++">` requires adjacency.

### Implied tokens

`implied(tok, cond)` reads `tok`, or inserts it where `cond` holds and the
parser has no normal continuation for the current token, as in
`implied(lit<";">, gap::line | &lit<"}"> | eof)`. **Any normal continuation
blocks insertion**, even one that fails later: with a statement-ending
`implied(lit<";">, gap::line | eof)`, `a \n (b)` stays a call.

`tok` is an unguarded literal, or a terminal whose body is one; an inserted
token has zero width. `cond` combines gap conditions, `&e`, `!e` and `eof`
with `|` and `+`.

### Trivia scopes

`skip(ignore<Ts…>, hide<Ts…>, e)` replaces the trivia **between `e`'s
tokens**: the terminals in `ignore<…>` are dropped, those in `hide<…>` kept.
`tight(e)` is `skip(ignore<>, hide<>, e)`, so
`tight(lit<"["> + ref<ID> + lit<"]">)` rejects `[ name ]`. Called rules
inherit the scope; trivia before and after `e` belongs to the caller.
Readings that give one point different scopes are
[refused](refusals.md#regionboundaryshared).

### Indentation

`indent` and `dedent` stand alone in rules, or as a terminal's whole body.
They compare the leading spaces and tabs of content lines as **exact
strings**, without tab expansion:

- An equal run keeps the level; a longer run extending it opens a level.
- A run equal to an enclosing level closes every level above it, with one
  `dedent` each.
- Any other run, or a form feed in a run, is an indentation error.
- The end of input closes every level.

`using indent_newline = NEWLINE;` names the statement-ending terminal; blank
and comment-only lines do not change levels when `NEWLINE` reads them.
`indent_open` and `indent_close` name the brackets inside which indentation
and `NEWLINE` are ignored: a spelling, a choice of spellings, or a terminal.

```cpp
struct WS : terminal<some(cr<R"( \t)">)> {};
struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
struct NEWLINE
    : terminal<eol + many(many(cr<R"( \t)">) + option(ref<COMMENT>) + eol)
               + option(many(cr<R"( \t)">) + option(ref<COMMENT>) + eof)> {};
using indent_newline = NEWLINE;
static constexpr auto indent_open = lit<"("> | lit<"["> | lit<"{">;
static constexpr auto indent_close = lit<")"> | lit<"]"> | lit<"}">;
```

`NEWLINE` absorbs the blank and comment-only lines after a line ending,
including a last one with no line ending; `WS` and `COMMENT` stop before
`eol`. Trivia that consumes a backslash with a line ending joins lines.

## Advanced rules

### Character class details

`cr` uses regex bracket syntax without the brackets; write it as a raw
string, such as `cr<R"(\s)">`. `a-z` is a range, and `-` is literal first,
last, or right after a range. A leading `^` complements; elsewhere it is
literal.

| Escape | Meaning |
|---|---|
| `\n`, `\r`, `\t`, `\f`, `\v`, `\b`, `\0`, `\cX` | Control characters: `\b` is backspace, `\0` NUL when no digit follows, `\cX` a control letter |
| `\xHH`, `\uHHHH`, `\u{H…}` | A Unicode scalar, never a raw byte |
| `\d`, `\w`, `\s`; `\D`, `\W`, `\S` | `0-9`, `a-zA-Z0-9_`, space/tab/LF/CR/FF/VT; their complements |
| `\` before punctuation | The punctuation itself |

Unknown escapes, reversed ranges, shorthands as range ends (`a-\d`),
surrogates and values past U+10FFFF do not compile. `a | b` of two classes is
one class; `~(a | b)` complements it over all characters, invalid bytes
included. For strictly valid UTF-8, `cr<R"(\u{0}-\u{10FFFF})">` reads one
scalar. There is no normalization or grapheme grouping.

### Captures

`mark<Tag>(run)` remembers exact bytes, where `run` is a class or a
one-character literal, optionally under `many`, `some` or `repeat`.
`same<Tag>` reads the stored bytes again:

```cpp
struct Eq {};
through(lit<"["> + mark<Eq>(many(lit<"=">)) + lit<"[">,
        lit<"]"> + same<Eq> + lit<"]">)
```

This reads `[[x]]` or `[=[x]=]` with matching delimiter runs. Captures stay
within one token and do not cross terminal calls; a later mark replaces the
value. Every path must mark a tag before `same`
([`echoUnmarked`](refusals.md#echounmarked)).

### Recursive terminals

A terminal may call itself only if every cycle consumes input first
([`terminalCycle`](refusals.md#terminalcycle)), and never calls a parser rule
([`terminalReadsRule`](refusals.md#terminalreadsrule)). In recursive
delimited text, exclude both delimiters from the character fallback, so it
neither swallows an unfinished opener nor runs past a closer:

```cpp
struct COMMENT
    : terminal<lit<"/*"> + many(ref<COMMENT> | (!lit<"/*"> + !lit<"*/"> + any))
               + lit<"*/">> {};
```

### Left recursion

A left-recursive rule calls itself before reading a token. Both engines
support direct and indirect left recursion, including a call inside or behind
an `option` or behind a `many`, and build the same AST; LL
[refuses](refusals.md#leftrecursion) some indirect cycles and a call inside
`many` or `some`. Use [infix](#operator-precedence) for binary operators.

In the short notation below, `A := x y | x` is a rule `A` that reads `x y` or
`x`. The non-recursive alternatives give the base, in written order, and the
parser then prefers another recursive suffix over stopping:

```text
S := S x | a x | a          means (a x | a) many(x)
E := E "!" | "-" E | a      means ("-" E | a) many("!")
A := option(A ".") ID       means A "." ID | ID
```

So `E` reads `-a!` as `-(a!)`, whatever the order of its alternatives. For
`(-a)!`, use separate levels: `E := E "!" | N`, `N := "-" N | a`.

Refused forms:

- [An alternative](refusals.md#selfboundedarm) that begins and ends with its
  own rule, such as `E "+" E`; use `infix` or levels such as
  `E := E "+" T | T`.
- [A cycle](refusals.md#unitproductioncycle) with no input progress, such as
  `A := A | x`.
- [Left recursion](refusals.md#hiddenleftrecursion) behind something that may
  match nothing, such as `A := B A x | y` with nullable `B`, or behind more
  than seven optional items.

### Embedded grammars

`section<G>(open, close)` reads the caller's token `open`, then `G`'s entry
as a whole document, then the caller's token `close`. Its value is the
entry's value, so it can be assigned, appended or
[adopted](#adoption-and-classes) like a rule call; in a data rule, its text
includes the delimiters.

```cpp
assign<&Cell::formula>(section<calc::Formula>(lit<"<expr>">, lit<"</expr>">))
```

Inside, `G` keeps its own trivia, keywords, `word_boundary` and rules,
whatever the host overrides, and sees only the section: its `eof` holds just
before `close`. The section ends at the first token boundary where `close`
matches; a missing `close` is an error. The tree holds the nodes of both
languages.

Refused: a guest token matching or beginning with `close`
([`sectionEndShared`](refusals.md#sectionendshared)), a point read both
inside and outside ([`sectionPointShared`](refusals.md#sectionpointshared)),
indentation on either side ([`sectionIndented`](refusals.md#sectionindented)),
and a terminal entry for `G`.

### Derived grammars

A `cursiv::extends<Base>` grammar may add rules, redeclare `entry`,
`ignored`, `hidden` or `word_boundary`, and
[override rules](../recipes/derived-grammars.md). Every call to an overridden
rule uses the new body, in which `super` stands for the old one:
`ref<Extra> | super` adds an alternative. An override keeps the rule's kind
(terminal or rule, node or value) and class family, and inherits its
converter unless it states one; without its own, a data rule's new body must
give a value of the same type. With `extends<A, B>`, anything declared or
overridden by both parents must be restated.

## Where expressions are allowed {#expression-contexts}

| Element | Terminal | Rule | Rule condition |
|---|---|---|---|
| `lit`, `kw`, `ikw`, classes, `any`, `byte`, `eol` | Adjacent input | One token | Allowed |
| `eof` | Test | Test | Allowed |
| `ref<T>`, terminal `T` | Inline body | Token `T` | Text of `T`, under [Conditions](#conditions) |
| `ref<D>`, data rule `D` | Refused | Value of `D` | Allowed unless recursive |
| `ref<R>`, AST rule `R` | Refused | Node of `R` | Refused |
| `+`, `|`, `option`, `many`, `some`, `repeat` | Allowed | Allowed | Allowed |
| `commit(p, b)` | Refused | Direct quantifier item | Direct quantifier item |
| `unordered(…)` | Refused | Allowed | Refused |
| `skip`, `tight`, gap conditions | Refused | Allowed | Allowed |
| `through` | Allowed | One token | One token |
| `as<X>(e)` | Whole body | Allowed | Allowed |
| `mark`, `same` | Allowed | Inside `through` or an inline terminal | Same restriction |
| `&e`, `!e` | Test adjacent input | [Conditions](#conditions) | Allowed |
| `implied`, `indent`, `dedent` | Only `indent`/`dedent`, as the whole body | Allowed | Refused |
| Directives, `section<G>(…)` | Refused | Allowed | Refused |

`eof`, conditions, `create`, absent options and implied tokens consume
nothing. `indent` and `dedent` have no width but count as consumed tokens, so
a loop reading one makes progress.

## Warnings and limits

The generator [warns](refusals.md#warned-not-refused) about valid expressions
that are likely mistakes or slow to parse; warnings never change the grammar,
except that a [repeated alternative](refusals.md#twinproductions) is dropped.
A **refused** form, including a support limit, fails grammar compilation
([Grammar errors and fixes](refusals.md)). **Rejected** input, such as invalid
indentation or a chain at a `nonassoc` level, is an error in the document.
