# Grammar reference

You declare a grammar as a C++ type derived from `cursiv::GrammarSpec`. The
names on this page, such as `lit`, `many` and `assign`, are its members, so
you write them inside the grammar. Terminals read text, rules combine tokens,
and directives fill your AST. To learn step by step, start with
[Write a grammar](../learn/write-a-grammar.md).

The generator refuses a grammar it cannot read; see
[Grammar errors and fixes](refusals.md). Snippets assume the surrounding
grammar and AST declarations. Examples show what the grammar accepts or
rejects before error recovery repairs the input.

| To… | See |
|---|---|
| Match identifiers, numbers or comments | [Text](#text) |
| Know how the text is split into tokens | [Tokens](#tokens) |
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
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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

On `let x = 1; let y = 2;`, this builds a `Module` with two `Variable` nodes.
On empty text, it builds a `Module` with no variables. `assign` stores one
value, `append` adds one element, and the members they name decide the AST
class of each rule.

Naming advice: put AST classes in an `ast` namespace, so that each rule can
use its class's name without a `Rule` suffix. Write terminals in UPPER_CASE
and fragments in snake_case. For a complete project, follow
[Build a language end to end](../learn/walkthrough.md).

## Rules and grammar settings

A **terminal** reads one token, such as an identifier or a number. A **rule**
combines tokens, with **trivia** (whitespace and comments) allowed between
them, and may build an AST node. You call either with `ref<R>`.

| Declaration | Meaning |
|---|---|
| `terminal<Body>` | One token, with adjacent input. Its value is the matched text unless a converter wraps the whole body. |
| `rule<Body>` | Tokens, with trivia allowed between them. |
| `terminal<as<X>(Body)>`, `rule<as<X>(Body)>` | A terminal or data rule whose text is [converted](#conversion). |
| `ref<R>` | A call to a terminal or rule. `ref<terminal<…>>` and `ref<rule<…>>` declare one in place. |
| `infix<Primary, T, Levels>` | Operator expressions with [precedence](#operator-precedence). |
| `static constexpr auto f = e;` | A **fragment**: `e` is copied wherever you use `f`. See [Reuse grammar pieces](#reuse-grammar-pieces). |

An **AST rule** builds or returns one node. A **data rule** returns a value
instead:

- a body that is a single call returns the value of the called rule;
- an alternative returns the value of the branch it took, and all branches
  must give the same type;
- any other body returns the text of its tokens, joined without whitespace or
  comments.

```cpp
struct QualifiedName : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
struct Visibility : rule<as<Vis::Public>(kw<"public">)
                         | as<Vis::Private>(kw<"private">)> {};
```

`a . b` gives `a.b`, and `private` gives `Vis::Private`.

| Setting | Meaning |
|---|---|
| `using entry = R;` | The rule that reads a whole document. `R` builds a node, except that a [section](#embedded-grammars)'s entry may be a data rule. |
| `using ignored = T;` or `list<T…>` | Trivia dropped from the CST. |
| `using hidden = T;` or `list<T…>` | Trivia kept in the CST, so that comments can be attached to the next node. |
| `static constexpr auto word_boundary = c;` | The characters that continue a word; `w` by default. |
| `static constexpr bool case_insensitive = true;` | Every literal and keyword ignores letter case, as `ikw<>` does; `false` by default. See [Case-insensitive languages](../recipes/case-insensitive.md). |
| `indent_open`, `indent_block`, `indent_close`, `indent_newline`, `indent_newline_in` | [Indentation](#indentation) settings. |

A trivia terminal must consume input, and belongs to `ignored` or `hidden`,
never both.

## Text

### Characters

A character class reads one character: one Unicode scalar in valid UTF-8, or
one invalid byte. [Character class details](#character-class-details) covers
escapes and Unicode.

| Spelling | Reads |
|---|---|
| `cr<"a-zé_">` | one member of the class; `cr` stands for *character range* |
| `cr<"^…">`, `~c` | one character outside the class |
| `uc::L`, `uc::Lu`, `uc::Nd`, … | a Unicode general category or subcategory |
| `uc::XID_Start`, `uc::XID_Continue`, `uc::White_Space`, `uc::Alphabetic` | a Unicode property |
| `s`; `S` | one whitespace character, Unicode's included (no-break spaces, line separators, the byte order mark); its complement |
| `w`, `d`; `W`, `D` | ASCII word and digit characters; their complements |
| `any`; `byte` | one character; one raw byte |

### Literals and keywords

| Spelling | Reads |
|---|---|
| `lit<"…">` | these exact bytes, never empty; use it for punctuation, such as `lit<";">` |
| `lit<"…", G>` | the literal, only if the guard `G` holds right after it: `lit<"=", !lit<"=">>` |
| `kw<"…">` | a literal with a word-boundary test when its last character belongs to `word_boundary` |
| `ikw<"…">` | the same, ignoring letter case: `ikw<"if">` also matches `IF` (see [Case-insensitive languages](../recipes/case-insensitive.md)) |
| `eol` | CRLF, LF or CR; not the end of input, so write `eol | eof` to accept either |
| `eof` | the end of input, after any trailing trivia |

`word_boundary` defaults to `w` (ASCII letters, digits and underscore).
`kw<"if">` matches `if` only when the next character is outside that class
or the input ends. A spelling ending in punctuation has no such test, so
`kw<"(">` can match immediately before a name. For Unicode identifiers,
use a continuation class such as `uc::XID_Continue | cr<"_$">`.

Use `kw<"if">.doc<"…">()` to add hover text. A keyword can share one
description across the grammar or have a different description at each use.

Keywords are contextual: a word reads as a keyword only where the grammar
expects that keyword ([Tokens](#tokens)). Where only an identifier is
expected, `type` reads as an `ID`; where `kw<"type">` is expected too, it
reads as the keyword. To accept it as a name there, write
`ref<ID> | kw<"type">`. To reserve words, keep the grammar permissive and
report identifiers spelled like reserved words in a
[validation check](../recipes/validation.md).

### Terminals

Inside a terminal, a call to another terminal reads its text as part of the
same token: `terminal<ref<ID> + lit<":"> + ref<ID>>` reads `a:b` as one token.
A rule with the same body reads three tokens and allows trivia between them.
A terminal used as a token or as trivia must consume input, unless its whole
body is `indent` or `dedent`. A terminal used only in a condition may match
nothing. See also [Recursive terminals](#recursive-terminals).

### Scanning bytes with a function

Use `scan<F>` inside a terminal when its length comes from the input. For
example, a byte string starting with `5:` has exactly five payload bytes.
Define the scanner before the grammar:

```cpp
#include <charconv>
#include <string_view>

inline std::size_t read_byte_string(std::string_view input) {
  if (input.empty()) return 0;
  const char* const end = input.data() + input.size();
  std::size_t count;
  const auto [colon, error] = std::from_chars(input.data(), end, count);
  if (error != std::errc{} || colon == end || *colon != ':') return 0;
  const auto header = static_cast<std::size_t>(colon + 1 - input.data());
  return count <= input.size() - header ? header + count : 0;
}
```

In the grammar, `struct DATA : terminal<scan<read_byte_string>> {};` reads
`5:abcde` or `0:` as one token and rejects the truncated `5:abc`. You can
compose the scanner with other expressions, for example
`terminal<lit<"data="> + scan<read_byte_string>>`.

The function sees the remaining input at its position and returns the number
of bytes consumed. Zero or a count beyond the view means no match. The
parser may try it more than once, so keep it deterministic, free of side
effects and quick to reject an unrelated prefix.

### Through

`through(open, close)` reads delimited text. It means
`open + many(!close + any) + close`, so it stops at the first `close`:

```cpp
struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
```

On `/* a */ b */`, the token ends at the first `*/`. To accept a missing
closing delimiter, write `close | eof`: `through(lit<"//">, &eol | eof)` stops
before the line ending or at the end of input. In a rule, `through` reads one
token.

## Tokens

The parser reads the text one token at a time. A terminal that reads one fixed
word or text is a keyword or a literal; any other, such as `some(d)`, is a
pattern.

| Rule | Example |
|---|---|
| Only the terminals that can come next are tried, plus trivia and what the [conditions](#conditions) there name. | Where only `ID` is expected, `if` reads as an `ID`, even if the grammar uses `kw<"if">` elsewhere. |
| The longest match wins. At equal length, a token beats trivia, and keywords and literals beat patterns. A keyword also beats trivia that starts with the same whole word. | Where `kw<"if">` and `ID` are both expected, `if` reads as the keyword and `iffy` as an `ID`. |
| Other ties keep every match: each parse goes on with the token it accepts. | With `LOW : terminal<some(cr<"a-z">)>` and `NAME : terminal<some(cr<"a-zA-Z">)>`, `(ref<LOW> + lit<";">) | (ref<NAME> + lit<":">)` accepts `abc:`. |
| A token is never split once read. Where the grammar is ambiguous, each possible parse reads its own token. When several reach the end, the first place where they read different tokens chooses the longer, then the keyword or literal; where their tokens begin apart, the grammar's order chooses. | `(lit<"a"> + lit<"b">) | (lit<"ab"> + lit<"c">)` rejects `ab`. In `g(a<b<c>>::new)`, `>>` is two tokens for a type and one for an expression. |

If two trivia terminals describe the same text, use one terminal everywhere.

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
`many(append<&Block::statements>(ref<Statement>))`. A trailing separator must be
written out: `option(some(e, sep) + option(sep))`. In a rule, a repeated item
should consume input on each turn. Use `many(x)` for an optional list of
items that each read `x`. Large counted repetitions increase parser size.

### Unordered groups

`unordered(a, b, …)` reads each of its items once, in any order. It needs at
least two and accepts at most eleven. Use `option(x)` for an optional item
or `many(x)` for a contiguous run of zero or more items. A bounded optional
run such as `repeat<0, 3>(x)` cannot be an unordered item.

| Expression | Accepted input |
|---|---|
| `unordered(a, b)` | `a b` or `b a` |
| `unordered(option(a), b)` | `b`, `a b` or `b a` |
| `unordered(some(a), b)` | `a a b` or `b a a`, but not `a b a` |

For items that may repeat anywhere, collect them with
`many(append<&ast::Box::settings>(ref<Setting>))` and report duplicates in a
[validation check](../recipes/validation.md). See [grammar limits](refusals.md#grammar-size-limits).

### Choosing between parses

The parser keeps competing readings until the input or the grammar decides
between them. When several complete parses remain, their
[tokens](#tokens) decide first. Parses that read the same tokens are compared
by their decisions from left to right, outer before nested. The grammar's
order decides at the first difference.

| Construct | Preferred decision |
|---|---|
| Choice | First written alternative |
| Option | Present |
| Repetition | Another item |
| Unordered group | Items in written order; optional items present |

A terminal takes its longest match instead, and
[left recursion](#left-recursion) has its own order. An alternative that reads
the same tokens as an earlier one loses that tie, even when it writes different members.

So with `option(kw<"else"> + ref<Statement>)` at the end of an `if` rule,
`if a if b x else y` gives the `else` to the nearest `if`.
[`commit`](#committed-items) does the same, but removes the other reading.
Factoring can change which property is written. Suppose `A` reads `x y | x`,
`B` reads `y z` and `C` reads `z`. On `x y z`, `a=A b=B | a=A c=C` selects
the first outer alternative and fills `b`. In `a=A (b=B | c=C)`, the first
alternative of `A` consumes `x y`, so the following `z` fills `c`.

Run the language generator with `CURSIV_CONFLICTS=1` to see local LR
conflicts, their lookahead, and the grammar declarations and properties involved.
A local conflict identifies a choice at a parser state. Establishing ambiguity
requires a complete input accepted through both readings.

## Building the tree

Directives store parsed values in your AST members, as in the
[quick start](#quick-start).

| Directive | Effect |
|---|---|
| `create<T>`, `create<T>(e)` | Gives the [current node](#nested-assignments) the class `T`, keeping the values already written. `create<T>(e)` is `create<T> + e`. |
| `assign<&T::m>(e)` | Stores the [value](#expression-values) of `e` in the single-valued member `m`. |
| `append<&T::m>(e)` | Adds the value of `e` to the `vector` member `m`. |
| `flag<&T::m>(e)` | Reads `e`, then sets the `boolean` member `m` to `true`. |
| `nest<&T::m>(e)`, `nest<W, &T::m>(e)` | Wraps the current node in a new `T` (or `W`, derived from `T`) that holds it in `m`; `e` then writes into the new node. |
| `assign<xref<&T::m, N>>(e)` | Assigns a reference that links only to targets of class `N`, a class derived from one the reference accepts. |

A rule builds the most derived class that declares the members it writes. A
member pointer names the class that **declares** the member, not the class
written before `::`: if `Base` declares `name`,
`kw<"derived"> + assign<&Derived::name>(ref<ID>)` builds a `Base`. Write
`create<T>` to build another class, to pick one when the members come from
unrelated classes, or to build a node in a rule that writes no member:

```cpp
create<Derived> + kw<"derived"> + assign<&Derived::name>(ref<ID>)
create<Null>(kw<"null">)
```

Every node must be returned by its rule or stored in an AST member.

### Nested assignments

Each rule call, and each operand of `assign` or `append`, builds its value in
its own **construction context**. The node that its directives write is its
**current node**. Sequences, alternatives, repetitions and `flag` stay in the
current context.

So `assign<&A::m>(assign<&B::n>(e))` stores in `A::m` a `B` with `n = e`.
`assign<&A::m>(option(assign<&B::n>(e)))` always stores a `B`, which is empty
when `e` is absent. To store nothing in that case, put the `option` outside
the outer `assign`.

### Adoption and classes

A call to an AST rule outside `assign` or `append` returns that rule's node:
this is **adoption**. `lit<"("> + ref<Expression> + lit<")">` returns the
`Expression` node, with no wrapper for the parentheses. Put `assign` or
`append` around the call to store the node in a member instead.

A **path** is one way through a rule's body: one alternative at each choice
and one number of turns at each option or repetition. On a path, an adoption
cannot come after a write, a `create`, a `nest` or another adoption in the
same construction context. Store the node with `assign` or `append` instead,
or write the shared part as a [fragment](#reuse-grammar-pieces). After an
adoption, writes and `nest` may follow, but not `create`. A rule must return
a node on every path, so `option(ref<Expression>)` alone is refused.

### Writes and wrapping

A single-valued member is written **at most once per node and path**,
counting writes by an adopted rule: `many(assign<&T::m>(…))` is refused unless
a `nest` makes a new node on each turn. Directives inside a
[condition](#conditions), a `through` or a `mark` are refused, because they
would never run.

`flag<&T::m>(e)` sets the property whenever `e` succeeds, including an empty
match. The generator warns when `e` can succeed without consuming a token,
including through a rule call. For a presence flag, initialize the property to
`false` and write `option(flag<&Decl::isStatic>(kw<"static">))`. Both `indent`
and `dedent` consume a parser token even at a zero-width source position.

`nest` wraps the current node, so a `create`, an adoption or a write must come
before it. Its member must be able to hold that node:

```cpp
ref<Name> + many(nest<&Index::array>(
    lit<"["> + assign<&Index::index>(ref<Expression>) + lit<"]">))
```

`a[0][1]` gives `Index(Index(Name(a), 0), 1)`. Such a rule returns the most
derived common base of its possible results, here of `Name` and `Index`.

## Expression values

The value of an operand of `assign`, `append` or an
[infix operator](#operator-precedence):

| Expression | Value |
|---|---|
| An operand that builds a node | Its final current node. |
| A [token](#tokens), rule or `section` | Its value, after its converter. |
| An alternative | The value of the alternative that matched. |
| A sequence that builds no node | Its text, joined as in a [data rule](#rules-and-grammar-settings). |
| Optional or repeated text | Put the directive inside the quantifier to write once per matched item. |

So `assign<&T::name>(ref<ID> + many(lit<"."> + ref<ID>))` stores `a.b.c`. Write `option(assign<&T::n>(ref<NUMBER>))` to leave the
member untouched when the number is absent.

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
| A type `T` | `cursiv::default_converter<T>` converts text to `std::string_view`, `std::string`, booleans, sized integers, floating-point values (`float`, `double`) or an enum by name. Specialize it with `static ConversionResult<T> convert(std::string_view text)` to support your own type in both `as<T>` and assignments. |
| A function `f` | `f(text)`, `f(parts)` or `f()`. |
| Another constant | The constant itself, such as an enumerator. |

`text` is a `std::string_view`. `parts` is a
`std::span<const std::string_view>` of a data rule's tokens and data-rule
calls, such as `{"a", ".", "b"}` for `a.b`. Give each parameter an explicit
type: an `auto` parameter, as in a generic lambda, is refused. A function can
return any type its member can hold, such as a `std::string`, a `struct Uuid` or
a class of your own. It can also return the member's own `vector<T>`, `optional<T>`
or `variant<...>`, of values, written whole with `assign`. `parts` is valid only
during the call.

`as<T>` makes a `T` first and the member takes that value, as with a function:
`as<std::int8_t>` on an `int64` member reports `128` as out of range. Use a member wide enough for the converter’s result.

Automatic enum conversion looks up names for values from −64 through 511 by
default. For other values or spellings, map the keyword explicitly, as in
`as<Status::Ready>(kw<"ready">)`.

A converter returns a value or a `ConversionResult`. `::ok(v)` gives a value.
`::err("message")` reports a diagnostic over the matched text; the member then
keeps its initial value, or `append` adds nothing. Converters run only for the
selected parse, never in conditions.

`as` can also give a value to part of a rule, as in the `Visibility` rule
[above](#rules-and-grammar-settings), without changing its tokens: `private` stays a keyword.
Use `rule<as<true>(kw<"on">) | as<false>(kw<"off">)>` when the branches
give different values. A branch can document its keyword with
`as<true>(kw<"on">.doc<"Enables the feature.">())`. The spelling, hover
text and completion stay attached to the keyword. A terminal accepts one
converter around its entire lexical pattern. Nested conversions belong in
one function, and the operand of a text conversion must produce text.

## Operator precedence

`infix<Primary, T, Levels>` reads operator expressions over `Primary`. List
the levels in `levels(…)`, from the tightest-binding to the loosest. Binary
levels are `left`, `right` or `nonassoc`. A `prefix` or `postfix` level reads
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
binds tighter than it. A prefix may begin any operand, whatever stands on its
left, and a postfix may end any operand, whatever stands on its right:

| Input | Grouping |
|---|---|
| `a + b * c` | `a + (b * c)` |
| `a - b - c` | `(a - b) - c` |
| `a < b < c` | A diagnostic at the second `<` |
| `-a * b`, `-a++` | `(-a) * b`, `-(a++)` |
| `not a < b and c` | `(not (a < b)) and c` |
| `a < not b`, `a * not b < c` | `a < (not b)`, `a * (not (b < c))` |

`infix` does not add parentheses: if your language has them, `Primary` must
read them, and a full expression starts again inside them. A single level
needs no `levels`, but each argument of `levels` must be a level; a bare
operator does not compile. A prefix and a binary operator can share a spelling,
as in `-a` and `a - b`. Give each binary operator one precedence and associativity.
Where a text reads both as a prefix operator with its operand and as a
primary, the prefix operator is read. A token that begins a binary operator
is read as that operator wherever the levels allow one. If it can also follow
the expression, like a newline that ends a statement, put its operator in the
loosest level of its `infix` rule.

`Primary` must [build a node](#building-the-tree); a lone primary returns it.
A binary operator builds a `T` and writes its members `left`, `op` and
`right`. `prefix<U>` and `postfix<U>` build a `U` and write its `op` and
`operand`. These names apply even when a base class declares the members. To
use other members, name them after the levels, or write
`prefix<&U::op, &U::operand>(op)` and `postfix<&U::operand, &U::op>(op)`,
which build the class that declares `op`:

```cpp
struct Assignment
    : infix<Primary, ast::Assign, right(lit<"=">),
            &ast::Assign::target, &ast::Assign::op, &ast::Assign::value> {};
```

An operator is a literal, a terminal, a data rule or a sequence of them,
possibly with conditions. It supplies a value for the operator member. Trivia may occur between its
tokens unless `gap::none` forbids it: `lit<">"> + gap::none + lit<">">` reads
`>>` but not `> >`. The operator member stores the operator's
[expression value](#expression-values): `kw<"NOT"> + kw<"EQUAL">` gives
`NOTEQUAL`, or the enumerator `Op::NOTEQUAL` when the member is an
enumeration. Use [`as<V>(e)`](#conversion) for another value, such as
`left(as<Op::mul>(lit<"*">) | as<Op::div>(lit<"/">))`.

## Reuse grammar pieces

A fragment is copied into each rule that uses it, so its directives write that
rule's current node. A call to an AST rule would build a separate node
instead. Use fragments for the parts that several rules share:

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

A condition tests the input without consuming it or building anything: `&e`
requires that `e` matches, and `!e` requires that it does not. A condition
must hold where it appears; it does not choose between alternatives, `a | b`
does. In a rule, a condition tests the text of the next tokens. In a terminal
or a literal guard, it tests the bytes that follow. With an alphabetic `ID`:

| Condition | Holds on | Fails on |
|---|---|---|
| `!lit<"let">` before `ref<ID>` in a rule | `letter` | `let` |
| `!lit<"let">` in a terminal | `lex` | `letter`, whose bytes start with `let` |
| `&(ref<ID> + lit<"b">)` in a rule | `a b` | `ab` |
| `lit<"a", &lit<"b">>` | `ab` | `a b` |

A condition does not need to match up to the end of input; add `eof` to
require that. Test the end of input with `eof`, not `!any`. In a rule, the
first token of a condition must match the whole next token, not only its
start: `!lit<"<"> + lit<"<<">` reads `<<`. Bound a lookahead when the
language allows it to limit how far a condition reads. See [Where expressions are allowed](#where-expressions-are-allowed) for what a
condition may contain.

A condition in a rule also decides which token stands where it stands. The
token is the longest match among the terminals a viable parse can read there
and the spellings the conditions there name. A
terminal that only a refused condition would let the parse read is not viable,
so the text is read again without it:
`(gap::none + lit<"->"> + ref<ID>) | (lit<"-"> + lit<">"> + ref<ID>)` reads
` -> b` as `-`, `>` and `b`. A spelling that only a condition names stays one
token, which no terminal reads, so it is an error and is never read in pieces:
with `(!lit<"->"> + ref<ID>) | (lit<"-"> + lit<">">)` and no `->` terminal
there, `->` is an error and `- >` is read.

### Committed items

`commit(p, b)` is the direct item of `option`, `many`, `some` or a
`repeat<Min, Max>` with Min < Max: a quantifier that may stop early. It reads
`p + b`, but once the prefix `p` matches, the body `b` must follow: a missing
body is reported right after `p`. This can change the accepted input:

| Expression | On `x:1 y` |
|---|---|
| `many(ref<ID> + lit<":"> + ref<Value>) + ref<ID>` | Read: the repetition stops before `y`. |
| `many(commit(ref<ID>, lit<":"> + ref<Value>)) + ref<ID>` | Rejected: `y` matches the prefix, so `:` must follow. |

`many(commit(p, b))` reads like `many(p + b) + !p`, and `option(commit(p, b))`
like `(p + b) | !p`. So `option(commit(kw<"else">, ref<Statement>))` gives
each `else` to the nearest `if` and removes the other reading.

The prefix must consume input and may contain only what a condition allows,
so it cannot contain directives. Commit on a token you do not store, such as a
separator or a keyword, and read what you store in the body. In a separated
list, the separator and the prefix commit together, and the prefix is tested
on the text, like a condition. If a keyword such as `end` may follow a
trailing separator, guard the prefix against it, as in `!kw<"end"> + ref<ID>`.

## Layout

### Gap conditions

The **gap** is the text before the current token, back to the previous token
that has a width (or to the start of input): its whitespace and comments.

| Condition | Holds when |
|---|---|
| `gap::has(e)` | part of the gap matches `e` |
| `gap::is(e)` | the whole gap matches `e` |
| `gap::line` | the gap contains a line ending, outside an `indent_open` bracket |
| `gap::none` | the gap is empty; even a comment with no spaces around it makes it false |

`e` is a simple text pattern: a class, a literal, `eol` or a repetition of
them ([other shapes are refused](refusals.md#tokens-and-layout)). For example,
`kw<"return"> + option(!gap::line + ref<Expression>)` takes no operand from
the next line, and `ref<Operand> + gap::none + lit<"++">` requires the two to
touch.

### Implied tokens

`implied(tok, cond)` reads `tok` when it is there. When it is missing, the
parser inserts it if `cond` holds and the current token cannot continue the
parse in any other way, as in
`implied(lit<";">, gap::line | &lit<"}"> | eof)`. **Any other way to continue
blocks the insertion**, even one that fails later: with a statement-ending
`implied(lit<";">, gap::line | eof)`, `a \n (b)` stays a call.

Use a token such as `lit<";">` or `kw<"end">` for `tok`. Indentation brackets
must be written explicitly. Use `|` for alternatives in `cond`, or `+` to
combine one gap test with one token test (`&e`, `!e` or `eof`).

### Trivia scopes

`skip(ignore<Ts…>, hide<Ts…>, e)` sets the trivia **between the tokens of
`e`**: the terminals in `ignore<…>` are dropped, and those in `hide<…>` are
kept in the CST. `tight(e)` is `skip(ignore<>, hide<>, e)`, so
`tight(lit<"["> + ref<ID> + lit<"]">)` rejects `[ name ]`. Rules called
inside `e` use the same trivia. Trivia before and after `e` follows the
caller's settings. A grammar where the trivia before a token may belong to
the scope or not, depending on how the input is read, is
[refused](refusals.md#tokens-and-layout).

### Indentation

`indent` and `dedent` stand alone in rules, or as a terminal's whole body.
They compare the leading spaces and tabs of each content line as **exact
text**: a tab is never expanded to spaces.

- The same indentation keeps the level. A longer one that starts with the
  current one opens a level.
- An indentation equal to an outer level closes every level above it, with
  one `dedent` each.
- Any other indentation is an indentation error.
- A form feed starts the count again, as in Python: only the spaces and tabs
  after the last one count.
- The end of input closes every level.

`using indent_newline = NEWLINE;` names the terminal that ends a statement.
Blank lines, form feeds included, and comment-only lines do not change levels
when `NEWLINE` reads them.
`indent_open` and `indent_close` name the brackets inside which indentation
is ignored and line endings are blanks, to `NEWLINE` and to `gap::line`
alike: a spelling, a choice of spellings, or a terminal. Inside an
`indent_block` bracket, line endings count again, as in a language whose
blocks may stand between parentheses:

```cpp
static constexpr auto indent_open = lit<"("> | lit<"[">;
static constexpr auto indent_block = lit<"{">;
static constexpr auto indent_close = lit<")"> | lit<"]"> | lit<"}">;
```

With these, `f({ a \n b })` holds two statements, and `f(a \n + b)` one
expression. `indent_close` closes the innermost bracket, whatever its kind.
`static constexpr auto indent_newline_in = ref<ML_COMMENT>;` names trivia
whose line endings end a statement too, as Scala reads a block comment: where
`NEWLINE` may come, such a trivia holding a line ending reads as one before
the next token, if `NEWLINE` reads a line ending there.
Indentation is read only outside every bracket. A trivia terminal that reads
a backslash followed by a line ending joins the two lines.
[Indentation-sensitive languages](../recipes/indentation.md) shows a complete
grammar.

## Advanced rules

### Character class details

`cr` uses regex bracket syntax without the brackets; write it as a raw
string, such as `cr<R"(\s)">`. `a-z` is a range, and `-` is literal first,
last, or right after a range. A leading `^` complements the class; elsewhere
it is literal.

| Escape | Meaning |
|---|---|
| `\n`, `\r`, `\t`, `\f`, `\v`, `\b`, `\0`, `\cX` | Control characters: `\b` is backspace, `\0` NUL when no digit follows, `\cX` a control letter |
| `\xHH`, `\uHHHH`, `\u{H…}` | A Unicode scalar, never a raw byte |
| `\d`, `\w`, `\s`; `\D`, `\W`, `\S` | `0-9`, `a-zA-Z0-9_`, whitespace as `s` reads it; their complements |
| `\` before punctuation | The punctuation itself |

Unknown escapes, reversed ranges, shorthands as range ends (`a-\d`),
surrogates and values past U+10FFFF do not compile. `a | b` of two classes is
one class; `~(a | b)` complements it over all characters, invalid bytes
included. For strictly valid UTF-8, `cr<R"(\u{0}-\u{10FFFF})">` reads one
scalar. There is no normalization or grapheme grouping.

### Captures

`mark<Tag>(run)` stores the exact bytes that `run` reads, where `run` is a
class or a one-character literal, optionally under `many`, `some` or
`repeat`. `same<Tag>` reads the stored bytes again:

```cpp
struct Eq {};
through(lit<"["> + mark<Eq>(many(lit<"=">)) + lit<"[">,
        lit<"]"> + same<Eq> + lit<"]">)
```

This reads `[[x]]` or `[=[x]=]` with matching delimiters. A capture works
only inside one token and does not pass into or out of a called terminal. A
later `mark` of the same tag replaces the stored bytes. Every path must mark a
tag before `same` reads it. A terminal may use up to four capture tags.

### Recursive terminals

Read input before a recursive terminal call and give it a way to finish.
Terminals can call other terminals. Use parser rules to combine tokens. In nested
delimited text, keep both delimiters out of the `any` case, so that it reads
neither an unfinished opening delimiter nor a closing one as plain text:

```cpp
struct COMMENT
    : terminal<lit<"/*"> + many(ref<COMMENT> | (!lit<"/*"> + !lit<"*/"> + any))
               + lit<"*/">> {};
```

### Left recursion

A left-recursive rule calls itself before reading a token. The parser
supports direct and indirect left recursion, including a call inside or behind
an `option`, behind a `many`, or inside a `many` or `some`. Use
[infix](#operator-precedence) for binary operators.

In the short notation below, `A := x y | x` is a rule `A` that reads `x y` or
`x`. The non-recursive alternatives start the match, in written order. After
that, the parser prefers reading one more recursive suffix to stopping:

```text
S := S x | a x | a          means (a x | a) many(x)
E := E "!" | "-" E | a      means ("-" E | a) many("!")
A := option(A ".") ID       means A "." ID | ID
```

So `E` reads `-a!` as `-(a!)`, whatever the order of its alternatives. For
`(-a)!`, use separate levels: `E := E "!" | N`, `N := "-" N | a`.

A cycle such as `A := A | x` reads no input and is refused. Use
`infix` or separate levels such as `E := E "+" T | T` to give an
ambiguous expression an explicit grouping.

### Embedded grammars

`section<G>(open, close)` reads the caller's token `open`, then `G`'s entry
as a whole document, then the caller's token `close`. Its value is the
entry's value, so you can assign, append or
[adopt](#adoption-and-classes) it like a rule call. In a data rule, its text
includes the delimiters.

```cpp
assign<&Cell::formula>(section<calc::Formula>(lit<"<expr>">, lit<"</expr>">))
```

Inside the section, `G` keeps its own trivia, keywords, `word_boundary`,
`case_insensitive` and rules, whatever the host grammar overrides. `G` sees only the section: its
`eof` holds just before `close`. The section ends at the first token boundary
where `close` matches; a missing `close` is an error. The tree holds the nodes
of both languages.

Choose a closing token distinct from every embedded token and its prefixes.
The alternatives must distinguish the section before entering it. Both grammars
must be free of indentation and the embedded entry must be a rule.
See [sections and conditions](refusals.md#sections-and-conditions).

### Derived grammars

A `cursiv::extends<Base>` grammar can add rules, redeclare grammar settings,
and replace rules in one list. Each target appears once.

```cpp
using overrides = list<
    replace<Base::WS, terminal<super | lit<"~">>>,
    replace<Base::Label, rule<as<canonical>(super) | kw<"new">>>>;
```

Every call to the target uses its replacement. `super` denotes the effective
parent body at this level. In `Label` above only the inherited branch uses
`canonical`. Replacements keep the rule kind, return a compatible AST class
or data type, and inherit a whole-body converter unless they supply one.

With `extends<A, B>`, a replacement inherited from one common ancestor is
shared. When both parents supply their own replacement for a target, resolve
it explicitly in the child with a full body and supply a converter if the
parents' converters differ. See [Reuse a grammar](../recipes/reuse-a-grammar.md).

## Where expressions are allowed

| Element | Terminal | Rule | Condition in a rule |
|---|---|---|---|
| `lit`, `kw`, `ikw`, classes, `any`, `byte`, `eol` | Adjacent input | One token | Allowed |
| `eof` | Test | Test | Allowed |
| `ref<T>`, terminal `T` | Inline body | Token `T` | Text of `T`, under [Conditions](#conditions) |
| `ref<D>`, data rule `D` | Refused | Value of `D` | Allowed unless recursive |
| `ref<R>`, AST rule `R` | Refused | Node of `R` | Can test the text of a non-recursive rule without building its node |
| `+`, `|`, `option`, `many`, `some`, `repeat` | Allowed | Allowed | Allowed |
| `commit(p, b)` | Refused | Direct item of a quantifier that may stop early | Same restriction |
| `unordered(…)` | Refused | Allowed | Allowed |
| `skip`, `tight`, gap conditions | Refused | Allowed | Allowed |
| `through` | Allowed | One token | One token |
| `as<X>(e)` | Whole body | Allowed | Allowed |
| `scan<F>` | Reads bytes at the current position | One token | One token |
| `mark`, `same` | Allowed | Inside `through` or an inline terminal | Same restriction |
| `&e`, `!e` | Test adjacent input | [Conditions](#conditions) | Allowed |
| `implied` | Refused | Allowed | Allowed |
| `indent`, `dedent` | As the whole body | Read the level change | Read the level change in the enclosing rule |
| Directives, `section<G>(…)` | Refused | Allowed | Refused |

`eof`, conditions, `create` and absent options consume no input. An
`implied` token consumes its spelling when present and has zero width when
inserted. Each `indent` or `dedent` counts as one token at a zero-width
position.

## Warnings and limits

A grammar error stops the build. Warnings let generation continue, for
example when a presence flag can be set on empty input or an embedded token
may overlap its closing delimiter. See [Grammar errors and fixes](refusals.md).
