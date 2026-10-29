# Grammar errors and fixes {#refusals}

A **refusal** is an error in the grammar: the generator stops and builds no
parser. Search this page for the code or the first words of the message; the
quoted messages are excerpts. [Warnings](#warned-not-refused) do not stop
generation. LL and LR share the codes, but
[a few limits differ](#where-the-engines-differ).

## Rule errors {#refused-against-a-rule}

The message reads `<grammar>: refused (code)` and names the rule; for a cycle, it
also lists the rules involved.

### `nullableRepetition`

> rule `Root` repeats an item that may match nothing

A repeated item must read input, also through rule calls. Put the optionality
in the bounds: `many(x)`, not `many(option(x))`; `repeat<0, 3>(x)`, not
`repeat<2, 3>(option(x))`. For terminals, see
[`terminalNullableRepetition`](#terminalnullablerepetition).

### `nullableUnorderedItem`

> rule `Root` holds an unordered item that may match nothing, `"final"? "static"?` …

> rule `Root` holds an optional unordered item, … in a group whose items hold a `nest`

An item of `unordered(…)` must read input. To leave one out, write
`option(x)`, `many(x)` or `repeat<0, n>(x)` where `x` reads input. Above,
split the item in two: `option(kw<"final">), option(kw<"static">)`. When the
items contain a `nest`, no item may be optional: write the group with and
without it, `unordered(a, b) | a`.

### `ruleWithoutProduction`

> rule `Tail` derives nothing: every reading of it turned out impossible, so
> whoever calls it can never return

No alternative can succeed: a token is required after `eof`, for example, or
a recursion never ends. Keep at least one possible match.

### `twoEndMarks`

> rule `Root` asserts end of input twice in one choice

Remove the repeated `eof` from that alternative.

### `optionalEndMark`

> rule `Root` makes an end-of-input assertion optional

`option(eof)` restricts nothing. Write `eof`, or a choice such as
`eof | ref<Other>`.

### `optionalCondition`

> rule `Root` makes a condition optional

An optional condition restricts nothing. Require it, or put `option` around
the part it guards.

### `directiveOnNoPosition`

> rule `Root` writes a member from `eof` alone, or from nothing

`flag<&Doc::last>(eof)` writes from nothing. Put the directive on a part that
reads input, or on a choice with an `eof` branch.

### `skippedTerminal`

> rule `Root` names a terminal its reading skips as trivia

`ignored` or `hidden` skips this terminal at that point, so the rule never
sees it. Operators are checked too. Remove the call, or read it under
`tight(e)` or a `skip()` that does not skip it.

### `docConflict`

> rule `Root` documents one word twice with different prose

`kw<"if">.doc<"a">()` and `kw<"if">.doc<"b">()` are one token, and a token has
one hover text. Keep one.

### `precedenceClash`

> rule `Sum` names the same operator text on two levels

One text, such as `*`, stands on two levels of one table. Give it one level in
its table; another table may give it a level and a grouping of its own. A
condition on an operator does not make it different.

Allowed: operators that later tokens tell apart (`>` and `>` `>`, `not in` and
`not like`), and a prefix operator with the text of a binary one, such as
`-`. Two prefix levels, or two postfix levels, cannot share a text.

### `postfixBinaryClash`

> rule `Expr` reads `!` as a postfix operator on level 1 and as a binary
> operator on level 3 of its precedence table

After an operand, `!` could end it or join it to the next one. Give the two
operators different text.

> cannot compare the text of the postfix operator on level 1 with the binary
> operator on level 2 …

This check and [`precedenceClash`](#precedenceclash) compare prefix and
postfix operators by their text. When an operator is spelled through a
repetition, an `option` or a region, or in too many ways, the table is refused
wherever it could clash. Spell it with literals, terminals and data rules of
fixed text.

### `infixOperatorNotText`

> rule `Sum` names as an operator a rule that builds a node

An operator gives a value, not a node. Use a literal, a terminal, a data rule
or a sequence of these, with a converter if the operator member is not text.

### `tooManyPrecedences`

> rule `Expr` declares more than 255 precedence levels

Use at most 255 levels.

### `unitProductionCycle`

> rule `A` stands on a cycle of reductions that read no text (`A -> B`,
> `B -> A`, or `A -> A B` where `B` may match nothing)

LR only; LL reports [`leftRecursion`](#leftrecursion). Make the cycle read
input, or remove the redundant call.

### `hiddenLeftRecursion`

> rule `Type` reaches itself at its left edge behind an item that may match
> nothing (`A -> B A x` where `B` can be empty)

LR only; LL reports [`leftRecursion`](#leftrecursion). Make `B` read input, or
move it after the recursive call: `A -> A x` is fine. When the message names a
limit on optional items before the call, use fewer, group some behind an item
that reads input, or put the call first.

### `leftRecursion`

> rule `Expr` reaches itself before matching anything in a way the LL descent
> cannot fold

> rule `A` reaches itself before matching anything inside a repetition or
> behind optional items, past what the LL descent spells out

LL only. LL also refuses what LR reports as
[`unitProductionCycle`](#unitproductioncycle) or
[`hiddenLeftRecursion`](#hiddenleftrecursion). Spell a chain of binary
operators as `infix<>`.

LL reads a call inside or behind an `option`, or behind a `many`, up to seven
such items. Past that, or for a call inside `many` or `some`, write the
alternatives yourself, and group the optional items in front of the call
behind one that reads input: `X? Y? A z` becomes `(X Y? | Y) A z | A z`.

??? note "Indirect left recursion in LL"

    Some indirect cycles that LR reads need a rewrite for LL; the message
    names the cycle. Give one rule of the cycle an alternative that only calls
    another rule of the cycle, or inline one rule into the other:

    ```text
    A := B "x" | "a" "x"       B := A "y" | "a"       refused
    A := B | "a" "x"           B := A "x" | "a"       read
    ```

    When a rule of the cycle also calls itself directly, put that alternative
    first:

    ```text
    B := A "y" "z" | B "y" "w" | "b"       refused
    B := B "y" "w" | A "y" "z" | "b"       read, with the same trees
    ```

    Otherwise, split the cycle into explicit precedence levels, or use LR. LL
    also limits the size of a recursive cycle.

### `selfBoundedArm`

> rule `E`: the arm `E '+' E` begins and ends with `E`, which says nothing of
> how its operands nest …

`E := E + E | a` does not say whether `a+a+a` is `(a+a)+a` or `a+(a+a)`;
`E := E E | a` and `E := E "?" E ":" E | a` are refused too. Declare the
operators with [`infix`](grammar.md#operator-precedence), or give each level
its own rule: `E := E + T | T`, `T := T * F | F`. An alternative that begins
or ends with a token, such as `E := "-" E | a`, is fine.

## C++ type errors {#refused-by-the-compiler}

These errors come from a `static_assert` when the grammar compiles.

### A grammar named with `ref<>`

> `ref<G>` names a grammar: read it with `section<G>(open, close)`

Write `section<Json>(lit<"<">, lit<">">)`, so that the embedded text has an
end. A grammar that states indentation, or whose entry is a terminal, cannot
be embedded.

### A trivia list that names a rule

> `ignored`, `hidden` and `skip()` list terminals only: trivia are tokens the
> lexer skips, never rules the parser reads

Make the listed rule a `terminal<>`.

### A terminal in both trivia lists

> a terminal belongs to at most one of `ignored` and `hidden`: trivia are
> dropped or kept in the tree, never both

List the terminal once: in `ignored` to drop it, in `hidden` to keep it. The
same holds for a `skip()` and an embedded grammar.

### An unordered group of one item

> an unordered group needs at least two items: `unordered(a, b)`

Write the item alone.

### A level table that lists an operator

> levels(...) lists the levels of a table, tightest-binding first, each one
> `left(op)`, `right(op)`, `nonassoc(op)`, `prefix<...>(op)` or
> `postfix<...>(op)`

Write `levels(left(lit<"*">), left(lit<"+">))`, not
`levels(left(lit<"*">), lit<"+">)`.

### An infix rule with one or two members

> an infix rule names zero or three members: the left operand, the operator
> and the right operand

Name the three members, or none to use `left`, `op` and `right`.

### A commit outside a quantifier

> commit(prefix, body) stands only as the direct item of option, many, some or
> repeat

Write `many(commit(a, b))`, not `commit(a, b) + c`.

### A commit inside a terminal

> a commit stands in a rule, not in a terminal: a terminal reads adjacent bytes
> and has no condition to stop on

Use the plain repetition forms inside terminals.

### An implied token that is no unguarded literal

> `implied(tok, cond)` lays a token of no width, so `tok` is an unguarded
> literal or a terminal whose body is one

`implied(kw<"end">, eof)` and `implied(ref<NEWLINE>, eof)`, where `NEWLINE`
reads more than a literal, are refused. Imply a plain literal, or write
`ref<NEWLINE> | eof` where the token may be absent.

### A condition over a rule that builds a node

> A rule that builds a node is parsed, not read: no condition asks one

A condition reads text and builds nothing. Test the terminals the rule starts
with, or a data rule that spells them.

### A section in a condition or a terminal

> `section<G>(open, close)` stands in a rule, not in a condition …

A similar message names a terminal. Read the section in a rule, and test its
opening token in a condition: `&lit<"<">`.

### A condition over a rule that reaches itself

> a condition reads a data rule that reaches itself

Test what the rule starts with instead.

### An expression a condition cannot read

> `&e` and `!e` read `e` as the parse would: terminals, nonrecursive data
> rules, classes, literals and `any` …

Spell the orders of an unordered group as a choice,
`&((kw<"x"> + kw<"y">) | (kw<"y"> + kw<"x">))`, test the token itself rather
than `implied`, and read `indent` and `dedent` outside conditions.

A [commit](grammar.md#committed-items) prefix has the same limits, so a
directive there gets this message too. Put directives in the commit's body,
and commit on a token you do not need to store, such as a separator.

### A directive that would never run

> a create<>/assign<>/append<>/flag<> sits under a LOOKAHEAD … so this
> directive never runs

A condition builds nothing, and `through()` and `mark()`, which the message
may name instead, read inside one token: `&assign<&T::name>(ref<ID>)` never
writes. Put the directive around the whole part:
`assign<&T::name>(through(lit<"<">, lit<">">))`.

### An AST member that owns its text

> an AST member never owns its text

```cpp
struct Person : cursiv::Extends<cursiv::AstNode> {
  std::string name;        // refused
  string_view name;        // this
};
```

Keep the source text and `ParseResult` alive while reading the tree.

### A directive over a shape with no value

> stands over a shape that produces no value

A quantifier over no node, or a condition, has no value. Put the directive
inside, `option(assign<&T::n>(ref<NUMBER>))`, or name the quantifier as a
data rule and write that rule.

### A node written into a member that takes a value

> stands over a shape that builds a node, and the member takes a value

`assign<&T::name>(option(ref<Item>))` where `name` is a `string_view` and
`Item` builds a node. Give the member a node type, or write the part that
carries the value: `assign<&T::name>(ref<ID>)`.

### A directive over a repetition of nodes

> stands over a repetition that can hold SEVERAL nodes

Put the directive under the repetition:
`many(append<&T::m>(ref<Item>), lit<",">)`, not
`append<&T::m>(many(ref<Item>, lit<",">))`.

### A directive over two nodes

> stands over a sequence that can hold TWO nodes

`assign<&T::name>(option(ref<Qualifier>) + ref<Name>)`: create a node that
holds both, and write that.

### A list item that creates a node

> a create<> or nest<> as a separated-list item would absorb the whole list:
> name the item as a rule instead

Declare the item as a rule and append it:
`many(append<&List::items>(ref<Item>), lit<",">)`.

### An alternative that creates twice

> this alternative can create twice

A rule returns one node, and this alternative may build two:

- A repetition of nodes, `rule<some(ref<Item>)>`: wrap it,
  `create<List>(some(append<&List::items>(ref<Item>)))`.
- A `create` beside an unassigned call to an AST rule, or two such calls,
  also inside a `nest`: store each node with `assign` or `append`; `flag` does
  not store it.
- An unordered group whose items create nodes: create one node before the
  group and let the items fill it,
  `create<Node> + unordered(assign<&Node::a>(lit<"a">), assign<&Node::b>(lit<"b">))`.
- A `create` after a `nest`: put it before the nest, or choose the wrapper's
  class with `nest<MarkedBox, &Box::inner>(…)`.
- A precedence level written `ref<Product> + many(lit<"+"> + ref<Product>)`:
  declare the levels with [`infix`](grammar.md#operator-precedence).

### An alternative whose node may not exist

> this alternative hands over a node the text may leave out

In `rule<lit<"let"> + option(ref<Item>)>`, the rule returns the node of
`Item`, which may be absent. Put the node on every path, or create the rule's
own node around the optional part:
`create<Let>(option(assign<&Let::item>(ref<Item>)))`.

### A call after the node in hand was written

> this alternative calls a rule that builds a node after the node in hand was
> written, created or nested

An unassigned call makes the called node the current one, which works only
before anything is written. Hold the called node with `assign` or `append`,
or write the shared part as a [fragment](grammar.md#reuse-grammar-pieces) in
each alternative:

```cpp
static constexpr auto fn_body = kw<"fn"> + assign<&Decl::name>(ref<ID>);
struct Root : rule<option(flag<&Decl::exported>(kw<"export">))
                   + (create<Fn>(fn_body) | create<Var>(var_body))> {};
```

### A member written twice on one path

> this directive writes the member a second time on one path

> this repetition writes the member on every turn

> this directive writes a member the node of the rule called before it may have
> written already

Write a single-valued member once on each path through a node, writes of a
called rule included. For a list, put `append` inside the repetition:
`many(append<&Holder::items>(assign<&Part::n>(ref<ID>)))` builds one `Part`
per turn, while `append<&Holder::items>(many(assign<&Part::n>(ref<ID>)))`
writes one `Part`'s `n` on every turn.

### A class that derives from a node class without `Extends`

> this class derives from an AST base without `Extends`: a reference to the
> base would never resolve to it

Write `struct Guest : cursiv::Extends<Person> {}`, not
`struct Guest : Person {}`.

### A directive on the wrong storage

> `=` names the ONE value a member takes, and this member is a `vector<T>` list
> `+=` accumulates, so it names a member the grammar can accumulate INTO

Use `assign` for a single value and `append` for a `vector<T>` list.

### A `flag` on a member that is not a bool

> flag<&T::m>: the member must be bool

Make the member a `bool`.

### A directive over a shape with no node

> stands over a shape that can match with NO node

The part may hold no node of the member's class: it may be absent, read only
literals, build another class, or have its node taken by an inner directive.
Make the directive optional: `option(assign<&T::m>(ref<Part>))`.

### A nest with nothing in front of it

> this alternative has a nest<> with nothing in front of it

A nest wraps the current node, so a `create`, a call to an AST rule or a write
must come before it on every path.

### A nest whose member cannot hold the node in hand

> this nest buries the node in hand in a member that cannot hold it

The nest's member must accept every class the current node may have there,
earlier nests' wrappers included.

### A write the node in hand may not declare

> this directive writes a member of a class the node in hand may not be

`ref<Prim> + assign<&Typed::ty>(ref<ID>)` where `Prim` builds a `Name` or a
`Typed`: a `Name` has no `ty`. Adopt the subclass alone, or write a member
every adopted class declares.

### An override outside the family of its rule

> this override hands back a class outside the type of the rule it stands over

An override must build the rule's class or a class derived from it.

### An override that changes between AST and data rules {#an-override-that-changes-a-rules-genre}

> an override restates the rule's genre and this one changes it

Override an AST rule with a body that builds a node, and a data rule with one
that builds none.

### An override that changes a passed value

> this override passes a value of another type than the data rule it stands
> over

Without `as`, an override keeps the value type of the data rule:
`rule<as<V::c>(kw<"c">) | super>` over a rule passing a `V` is accepted,
`rule<kw<"c">>` is not.

### A literal the member can never read

> this literal can never convert

A literal such as `lit<"+">`, or a sequence such as `kw<"NOT"> + kw<"IN">`
(spelled `NOTIN`), written into a `bool` or enum member must spell one of its
values. Spell an enumerator, or give the value: `as<Op::add>(lit<"+">)`. Enumerators are found by name
only between `CURSIV_ENUM_PROBE_MIN` and `CURSIV_ENUM_PROBE_MAX` (-64 and 512
unless you define them).

### A converter whose result the member cannot take

> no conversion from this payload to the member type … `default_converter`

The terminal's converter gives a type the member does not accept.

### A converter function with an unclear input

> as<f>(e): f accepts more than one input

> as<f>(e): f reads neither the text … nor the parts … nor nothing

A function given to `as` reads `std::string_view` text, the parts, or nothing,
with an optional `std::pmr::memory_resource*` after them. Give a generic
lambda's parameter a type.

### An `as` inside an `as`

> as<X>(as<Y>(e)) is refused: compose the two conversions in one function

Write one function that does both conversions.

### An `as` inside a terminal body

> inside a terminal, as<X>(e) wraps the whole body or each of its alternatives

> inside a terminal, as<V>(e) on an alternative names a constant

> inside a terminal, an alternative with its own as<V>(e) reads words only

> inside a terminal, the constants of the alternatives' as<V>(e) have different types

In a terminal, `as` wraps the whole body: `terminal<as<V>(lit<"x"> + lit<"y">)>`.
To give each word its own value, write a data rule:
`rule<as<true>(ikw<"TRUE">) | as<false>(ikw<"FALSE">)>`.

### Data rule alternatives of different types

> the alternatives of this data rule pass values of different types

Give the whole rule one value, as in
`rule<as<std::string_view>(ref<NUM> | ref<ID>)>`, or make the branches agree.

### An `as` around a directive or a node

> as<X>(e) converts text, and this e writes the AST

> as<X>(e) gives a value to text, and this e builds an AST node

Move `assign`, `append`, `flag`, `create` and `nest` out of the `as`, and do
not wrap a rule that builds a node.

## AST list storage {#and-one-more-the-vocabulary-carries}

```cpp
vector<Person> persons;        // refused: a node is held by pointer
vector<std::string> names;     // refused: elements must be trivially copyable
std::vector<Person*> people;   // refused: use cursiv::vector
```

Each message names the fix.

## Lexical and layout errors {#refused-by-the-lowering}

### `regionBoundaryShared`

> the grammar reaches one point through a `skip()` region and outside it at
> once …

The trivia before a token could belong to the region or not. Part the
alternatives before the region opens, give the region an opening token of its
own, or end it on a token that no optional part or `eof` follows.

### `sectionPointShared`

> the grammar reaches one point inside a `section<>` and outside it at once

Two alternatives part only after the token that opens a section. Part them
before the section opens.

### `sectionEndShared`

> the grammar closes a `section<>` on a token the embedded grammar reads, or on
> the start of one

In `section<Expr>(lit<"<">, lit<">">)`, where `Expr` reads `>=`, the `>` of
`>=` could close the section. Close it on a token that no token of the
embedded grammar begins with.

### `sectionIndented`

> the grammar reads its lines by indentation and embeds a grammar with
> `section<>`

Embed sections only in a grammar that states no indentation.

### `indentTwice`

> terminal `OPEN` declares two indent terminals, or two dedent terminals

Declare `terminal<indent>` once and `terminal<dedent>` once.

### `indentShape`

> terminal `T` spells `indent` or `dedent` inside a body

Each is a terminal's whole body: `terminal<indent>`.

### Indentation settings that name no token

> `indent_newline` names the terminal that ends a line: a terminal a rule
> reads, not a rule or a terminal no rule reaches

Similar messages name `indent_open` and `indent_close`, which may also name
one token per bracket: `lit<"("> | lit<"[">`, not `lit<"("> + lit<"[">`.

### `gapShape`

> the grammar asks a condition of an expression the matcher does not read

`gap::has` and `gap::is` take classes, literals, `any`, sequences, choices and
repetitions, never `through`.

### `echoUnmarked`

> terminal `T` echoes a mark<> not set in its scope

Set the `mark<Tag>` before the echo on every path, in the same terminal call.
A mark in one branch of a choice, or in a loop that may not turn, does not
count.

### A mark that names no run

> a mark<> names a RUN -- one character class or one-character literal,
> repeated: `many(lit<"=">)`, `some(w)`, `repeat<0, 16>(cr<"^() ">)`

`mark<Eq>(lit<"ab">)` is refused: mark one character class or one-character
literal, alone or repeated.

### A capture outside a token

> this combinator has no lowering: a body reaching here would become a
> right-hand side nobody wrote

`mark` and `same` work inside one token. Use them in a `through` or a
terminal, not directly in a rule:

```cpp
through(lit<"["> + mark<Eq>(many(lit<"=">)) + lit<"[">,
        lit<"]"> + same<Eq> + lit<"]">)
```

### `triviaTied`

> terminal `NOTE` is trivia that reads the same text as another trivia terminal

Two trivia terminals match the same text at the same length and
[category](grammar.md#tokens), and one is `hidden`. Put both in `ignored`, or
make them read different texts.

### `terminalEmpty`

> terminal `T` matches the empty string -- give its body a part it must read

Write `some(d)`, not `many(d)`, and make the call optional in the rule where
the token may be absent. A terminal used only in conditions may match
nothing.

### `terminalCycle`

> terminal `T` calls itself without consuming input

Read input before the recursive call: `lit<"a"> + option(ref<T>)`, not
`ref<T> + lit<"a">`, `option(lit<"a">) + ref<T>` or `!ref<T> + any`.

### `terminalUnproductive`

> terminal `T` has a recursive call that cannot return a finite match

`T : terminal<lit<"a"> + ref<T>>` never ends. Add an alternative that ends, or
make the recursive part optional.

### `terminalNullableRepetition`

> terminal `T` repeats an item that may match nothing inside a recursive terminal

Write `many(lit<"b">)`, not `many(option(lit<"b">))`. Terminals that are not
recursive allow it.

### `terminalReadsRule`

> the grammar reads the rule `Pair`: a rule is read with the skipper between
> its parts, a phrase of tokens and never the bytes of one

A terminal reads one token, even through a data rule. Name the terminals the
rule is made of, or read the rule from a rule. A condition in a rule, such as
`&ref<Pair>`, may still name a data rule.

### An expression a terminal cannot read

> this expression cannot be read inside a terminal

A terminal reads one token, so it cannot hold an unordered group, `skip`,
`tight`, a gap condition, `implied` or a directive. Spell a group as a choice,
drop `skip`, `tight` and gaps or read those parts in a rule, and put `implied`
and directives in the rule that reads the terminal. A condition that reads a
data rule holding an unordered group gets the same message.

### `commitPrefixEmpty`

> rule `Root` commits on a prefix that may read nothing …

An empty prefix always matches, so the quantifier never stops. Give the
prefix a part it must read: `commit(option(lit<"x">) + lit<"a">, lit<"b">)`.

### `unorderedTooWide`

> rule `Root` holds an unordered group of too many items

A group holds at most eleven items, or six when an item contains `nest`. Use
fewer, or collect them as nodes, `many(append<&T::items>(ref<Item>))`, and
check uniqueness in validation.

## Advanced token restrictions

These rare errors concern negative guards in terminals. A terminal *refuses*
the word a guard such as `!kw<"if">` excludes, and the word-boundary class
*rescues* it inside a longer word: `!kw<"if">` allows `iffy`.

### `wordPastMatch`

> terminal `HASH` may end a match short of `'abc'`, a word it refuses past
> its head

`lit<"#"> | lit<"##", !lit<"abc">>`: the match `##` ends before `abc` is read.
Give the guarded literal a terminal of its own, or read the word whole before
the match may stop. A guard on a character class, such as `!uc::Lo`, gets this
message when a match may end inside one of its multi-byte characters.

### `wordRefusedInLoop`

> terminal `STARS` refuses a word inside a repetition that may come back to a
> refusal without reading a byte

Give the turn a part to read after the guard: `!lit<"/"> + any`.

### `rescuedRefusalPastMatch`

> terminal `W` ends a match exactly where the word it refuses ends, past its
> head, and `_` rescues that word

Put the guard at the start of the terminal, or after literals and character
classes only, or let the token read the characters that allow the longer
word.

### `refusalIntoRelay`

> terminal `CMT` refuses a word that runs into a relay's content

In a nested run, restrict the guard to the content's first byte, without a
rescue class.

### `rescueReadsPast`

> terminal `NAME` reads on past a word it refuses over a character its word
> boundary does not hold

Let the word boundary hold every character the terminal reads after the word.

### `boundaryDisagrees`

> terminal `T` reaches one match through two refusals of different classes

LR only. LL accepts this shape.

### `boundaryMisses`

> terminal `NAME` refuses `"in"` at its head, and reads `\` where a word of
> its own begins and again past `"in"`, yet the class that rescues `"in"`
> leaves `\` out

Let the boundary hold the character: `cr<R"(a-zA-Z0-9_\\)">`.

## Warnings {#warned-not-refused}

A warning does not stop generation. Only a
[repeated alternative](#twinproductions) changes the grammar: the generator
drops it.

### An alternative that repeats an earlier one {#twinproductions}

> warning: rule `Root`: branch 2 of 2 `b=ID` reads the same tokens as branch
> 1, so it is never chosen and is dropped …

> warning: rule `Root`: `(b=ID)?` reads the same tokens as the earlier
> `(a=ID)?`, so reading it in place of that item is never chosen and is
> dropped …

A choice takes the first alternative written, so an alternative that reads the
same tokens as an earlier one, whatever it writes, never wins; nor does an
optional item read in place of an earlier one. Merge the two, or reorder them
so that the one you mean comes first.

### A run nested in a run

> warning: rule `Root`: `(X+)*` repeats a run, so a run of items reads as one
> group or as several and the election picks one …

`many(some(ref<X>))`, even through a rule or a choice, groups the input in
several ways, and [parse selection](grammar.md#choosing-between-parses) picks
one. Write `many(ref<X>)`, with `append` inside to collect one child per item.

### Two runs side by side

> warning: rule `Root` reads a run of `NL` twice in a row: the run splits
> between the two in several ways -- write one repetition

LR only. An absent option between two runs splits them too:
`many(ref<X>) + option(ref<Y>) + some(ref<X>)`. Move the first run into the
option: `option(many(ref<X>) + ref<Y>) + some(ref<X>)`.

### An unbounded run in a condition

> warning: rule `Arrow` asks a condition of the token in hand that reads
> `('-')+` to any length: a completion then replays every token before the
> caret …

A condition with no maximum may read to the end of the document, which slows
completion; a run inside another repetition can also make parsing quadratic,
and the warning then says so. Give the run a maximum,
`!(repeat<1, 8>(lit<"-">) + lit<">">)`, or read it as one token,
`!ref<ARROW>` with `ARROW : terminal<some(lit<"-">) + lit<">">>`. A terminal
reads adjacent text only: in `- ->`, only `->` is then an arrow.

### A branch past `eof`

> warning: rule `Para`: branch 2 of 2 `ADORN !. EOL` is never read -- `EOL`
> stands after `eof`, which nothing can follow

The message spells `eof` as `!.`. Remove what follows `eof`, or let either one
end the branch: `ref<ADORN> + (ref<EOL> | eof)`. The warning also names an
option after `eof`, and an `eof` branch that a token follows.

### A flag that may read nothing

> warning: rule `Root`: `on?=("on"?)` reads an operand that may match nothing,
> so it always sets the member -- flag the token that tells, and make the flag
> optional

Write `option(flag<&Item::on>(kw<"on">))`, not
`flag<&Item::on>(option(kw<"on">))`, and initialize the member to `false`.

### A word literal with no guard

> warning: rule `Root`: `'begin'` is a literal with no guard that ends in a
> word character, so it also matches the start of a longer word, as
> `lit<"end">` matches `end` in `endx` -- write `kw<"begin">`, or give the
> literal a guard (`lit<"begin", G>`) if that is intended

Literals in terminals and conditions get no warning.

### A lexical check that cannot conclude

> warning: terminals `LONG` and `TAG` are trivia, and the check that they
> never read the same text at the same length and rank could not be completed …

The [`sectionEndShared`](#sectionendshared) and [`triviaTied`](#triviatied)
checks warn instead of refusing when a capture, a call, a nested run or a
condition stops them.

## Where the engines differ

- **LR reads, LL refuses** with [`leftRecursion`](#leftrecursion): some
  indirect left recursion, a left-recursive call inside `many` or `some`, and
  recursive cycles past LL's size limit.
- **LL reads, LR refuses**: one terminal match behind two guards of different
  classes ([`boundaryDisagrees`](#boundarydisagrees)), and more than 32
  implied tokens ([`gapTooMany`](#counts-a-program-cannot-hold)).
- **Different codes**: [`unitProductionCycle`](#unitproductioncycle) and
  [`hiddenLeftRecursion`](#hiddenleftrecursion) in LR are `leftRecursion` in
  LL, and the [size limits](#counts-a-program-cannot-hold) differ.
- Only LR gives the [two runs side by side](#two-runs-side-by-side) warning.

## Grammar size limits {#counts-a-program-cannot-hold}

These codes mean that the grammar is too large once generated, counting what
generation adds to your declarations.

| Code | The grammar… |
|---|---|
| `namePool` | declares too many distinct names |
| `tooManyTerminals` | declares more than 32 767 terminals |
| `tooManyRules` | declares more than 32 767 rules, generated ones included |
| `tooManyConfigurations` | uses more than 255 trivia settings: its own, one per distinct `skip()` and one per `section<>` |
| `gapTooMany` | states more than 255 gap conditions, or (LR only) more than 32 implied tokens |
| `gapLeafPool` | declares too many distinct condition expressions |
| `tokenGraphTooLarge` | has tokens too complex to generate, or a repetition bound above 4 294 967 295 |
| `tooManyPositions` | exceeds LL's size limit |
| `tooManyProductions` | has too many alternatives for LR |
| `productionTooWide` | has an alternative too long for LR |
| `tableTooLarge` | exceeds LR's size limit |

Counted repetitions and unordered groups grow fast: in a rule,
`repeat<Min, Max>` costs about `Max` copies of its item. Use an unbounded
repetition and check the count in validation, reduce alternatives, factor
shared expressions, or split the grammar into separate languages. The same
fixes apply to messages without a code, such as
`refused by the 16-bit index of an LR state …`,
`refused, 70000 rules overrun sixteen bits`,
`refused by the record's stamp …` or, in LL,
``rule `Root` repeats a `nest` more than 64 times in a row``.
