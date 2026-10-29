# Grammar errors and fixes

A **refusal** is an error in your grammar: the build stops and no parser is
generated. Search this page for the code or the first words of the message;
the quoted messages are excerpts. [Warnings](#warnings) do not stop
the build. LL and LR use the same codes, but
[a few limits differ](#where-the-engines-differ).

## Rule errors

The message reads `<grammar>: refused (code)` and names the rule; for a cycle, it
also lists the rules involved.

### `nullableRepetition`

> rule `Root` repeats an item that may match nothing

A repeated item must read input, including through the rules it calls. Use
the bounds to make it optional: `many(x)`, not `many(option(x))`;
`repeat<0, 3>(x)`, not `repeat<2, 3>(option(x))`. For terminals, see
[`terminalNullableRepetition`](#terminalnullablerepetition).

### `nullableUnorderedItem`

> rule `Root` has an unordered item that may match nothing, `"final"? "static"?` …

> rule `Root` has an optional item, …, in an unordered group whose items hold a
> `nest`

An item of `unordered(…)` must read input. To make an item optional, write
`option(x)`, `many(x)` or `repeat<0, n>(x)` where `x` reads input. In the
first example, split the item in two: `option(kw<"final">), option(kw<"static">)`.
When the items contain a `nest`, no item may be optional: write the group
with and without that item, `unordered(a, b) | a`.

### `ruleWithoutProduction`

> rule `Tail` can never match any text: each of its alternatives is
> impossible, such as one that needs a token after `eof` or a recursion that
> never ends

No alternative of the rule can ever match: for example, it requires a token
after `eof`, or its recursion never ends. Give the rule at least one
alternative that can match.

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

`flag<&Doc::last>(eof)` reads no text, so it never writes the member. Put the
directive on a part that reads input, or on a choice with an `eof` branch.

### `skippedTerminal`

> rule `Root` names a terminal that is skipped as trivia at that point

At this point, `ignored` or `hidden` skips the terminal as trivia, so the
rule never sees it. This also applies to operators. Remove the call, or read
it inside `tight(e)` or a `skip()` that does not skip it.

### `docConflict`

> rule `Root` documents one word twice with different prose

`kw<"if">.doc<"a">()` and `kw<"if">.doc<"b">()` are one token, and a token has
one hover text. Keep one.

### `precedenceClash`

> rule `Sum` names the same operator text on two levels

One operator text, such as `*`, appears on two levels of one precedence
table, or with two groupings. Give each text one level and one grouping in a
table; another table may use the same text with its own level and grouping.
A condition on an operator does not make it a different operator.

These are allowed: operators that the next tokens tell apart (`>` and `>`
`>`, `not in` and `not like`), and a prefix operator with the same text as a
binary one, such as `-`. Two prefix levels, or two postfix levels, cannot
share a text.

### `postfixBinaryClash`

> rule `Expr` uses `!` as a postfix operator on level 1 and as a binary
> operator on level 3 of its precedence table

After an operand, `!` could either end the expression or join it to the next
operand. Give the two operators different texts.

> has two operators whose texts cannot be compared, the postfix operator on
> level 1 and the binary operator on level 2 …

This check and [`precedenceClash`](#precedenceclash) compare prefix and
postfix operators by their text. When an operator's text comes from a
repetition, an `option` or a region, or has too many spellings, the check
cannot compare it, so the table is refused wherever two operators could
clash. Write each operator with literals, terminals and data rules of fixed
text.

### `infixOperatorNotText`

> rule `Sum` names as an operator a rule that builds a node

An operator gives a value, not a node. Use a literal, a terminal, a data rule
or a sequence of these, with a converter if the operator member is not text.

### `tooManyPrecedences`

> rule `Expr` declares more than 255 precedence levels

Use at most 255 levels.

### `unitProductionCycle`

> rule `A` is part of a cycle of rules that call each other without matching
> any text (`A -> B` and `B -> A`, or `A -> A B` where `B` may match nothing)

LR only; LL reports [`leftRecursion`](#leftrecursion). The rules call each
other in a loop without reading text. Make the loop read input, or remove the
redundant call.

### `hiddenLeftRecursion`

> rule `Type` reaches itself before matching anything, behind an item that may
> match nothing (`A -> B A x` where `B` may be empty)

LR only; LL reports [`leftRecursion`](#leftrecursion). Make `B` read input, or
move it after the recursive call; plain left recursion such as `A -> A x` is
fine. When the message names a limit of optional items before the call, use
fewer, put some behind an item that reads input, or put the call first.

### `leftRecursion`

> rule `Expr` reaches itself before matching anything, in a way the LL parser
> cannot handle

> rule `A` reaches itself before matching anything inside a repetition or
> behind optional items, beyond what the LL parser supports

LL only. LL also refuses what LR reports as
[`unitProductionCycle`](#unitproductioncycle) or
[`hiddenLeftRecursion`](#hiddenleftrecursion). Spell a chain of binary
operators as `infix<>`.

LL accepts a left-recursive call inside or after an `option`, or after a
`many`, with up to seven such items in front of it. With more, or for a call
inside `many` or `some`, write the alternatives yourself, and put the
optional items in front of the call behind one that reads input:
`X? Y? A z` becomes `(X Y? | Y) A z | A z`.

??? note "Indirect left recursion in LL"

    Some indirect cycles that LR accepts need a rewrite for LL; the message
    names the cycle. Give one rule of the cycle an alternative that only calls
    another rule of the cycle, or inline one rule into the other:

    ```text
    A := B "x" | "a" "x"       B := A "y" | "a"       refused
    A := B | "a" "x"           B := A "x" | "a"       accepted
    ```

    When a rule of the cycle also calls itself directly, put that alternative
    first:

    ```text
    B := A "y" "z" | B "y" "w" | "b"       refused
    B := B "y" "w" | A "y" "z" | "b"       accepted, with the same trees
    ```

    Otherwise, split the cycle into explicit precedence levels, or use LR. LL
    also limits the size of a recursive cycle.

### `selfBoundedArm`

> rule `E`: the alternative `E '+' E` begins and ends with `E`, so it does not
> say how its operands group …

`E := E + E | a` does not say whether `a+a+a` is `(a+a)+a` or `a+(a+a)`;
`E := E E | a` and `E := E "?" E ":" E | a` are refused too. Declare the
operators with [`infix`](grammar.md#operator-precedence), or give each level
its own rule: `E := E + T | T`, `T := T * F | F`. An alternative that begins
or ends with a token, such as `E := "-" E | a`, is fine.

## C++ type errors

These errors have no code. Most come from a `static_assert` when the C++
compiler compiles the grammar; a few come from the generator.

### A grammar named with `ref<>`

> `ref<G>` names a grammar: read it with `section<G>(open, close)`

Write `section<Json>(lit<"<">, lit<">">)`, so that the embedded text has an
end. A grammar that uses indentation, or whose entry is a terminal, cannot be
embedded.

### A trivia list that names a rule

> `ignored`, `hidden` and `skip()` list terminals only: trivia are tokens the
> lexer skips, never rules

Make the listed rule a `terminal<>`.

### A terminal in both trivia lists

> a terminal belongs to at most one of `ignored` and `hidden`: trivia are
> dropped from the tree or kept in it, never both

List the terminal once: in `ignored` to drop it, in `hidden` to keep it. The
same holds for a `skip()` and an embedded grammar.

### An unordered group of one item

> an unordered group needs at least two items: `unordered(a, b)`

Write the item alone.

### A level table that lists an operator

> levels(...) takes only precedence levels, tightest-binding first: each one
> is `left(op)`, `right(op)`, `nonassoc(op)`, `prefix<...>(op)` or
> `postfix<...>(op)`

Write `levels(left(lit<"*">), left(lit<"+">))`, not
`levels(left(lit<"*">), lit<"+">)`.

### An infix rule with one or two members

> an infix rule names zero or three members: the left operand, the operator
> and the right operand

Name the three members, or none to use `left`, `op` and `right`.

### A commit outside a quantifier

> commit(prefix, body) must be the direct item of option, many, some or
> repeat, as in many(commit(a, b))

Write `many(commit(a, b))`, not `commit(a, b) + c`.

### A commit inside a terminal

> a commit is allowed in a rule, not in a terminal: inside a terminal, use
> option, many, some or repeat without commit

Use the plain repetition forms inside terminals.

### An implied token that is not a plain literal

> in `implied(tok, cond)`, `tok` must be a literal with no guard, or a
> terminal whose body is one

`implied(kw<"end">, eof)` and `implied(ref<NEWLINE>, eof)`, where `NEWLINE`
reads more than a literal, are refused: a keyword is a guarded literal. Imply
a plain `lit<>`, or write `ref<NEWLINE> | eof` where the token may be absent.

### A condition over a rule that builds a node

> a condition cannot test a rule that builds a node

A condition only tests text and builds nothing. Test the terminals the rule
starts with, or a data rule made of them.

### A section in a condition or a terminal

> `section<G>(open, close)` is allowed in a rule, not in a condition …

A similar message names a terminal. Read the section in a rule, and test its
opening token in a condition: `&lit<"<">`.

### A condition over a recursive rule

> a condition cannot test a data rule that calls itself, directly or through
> other rules

Test what the rule starts with instead.

### An expression a condition cannot read

> a condition (`&e` or `!e`) cannot test this expression: `e` may hold
> terminals, data rules that do not call themselves, classes, literals and
> `any` …

Write the orders of an unordered group as a choice,
`&((kw<"x"> + kw<"y">) | (kw<"y"> + kw<"x">))`, test the token itself instead
of `implied`, and read `indent` and `dedent` outside conditions.

A [commit](grammar.md#committed-items) prefix has the same limits, so a
directive in a prefix gets this message too. Put directives in the commit's
body, and commit on a token you do not need to store, such as a separator.

### A directive that would never run

> a create<>, assign<>, append<> or flag<> is inside a condition (`&` or `!`)
> … so this directive never runs

A directive inside a condition, a `through()` or a `mark()` never runs: a
condition builds nothing, and `through()` and `mark()` read inside one token.
For example, `&assign<&T::name>(ref<ID>)` never writes. Put the directive
around the whole part instead: `assign<&T::name>(through(lit<"<">, lit<">">))`.

### An AST member that owns its text

> an AST member never owns its text

```cpp
struct Person : cursiv::Extends<cursiv::AstNode> {
  std::string name;        // refused
  string_view name;        // this
};
```

A text member is a view into the source. Keep the source text and the
`ParseResult` alive while you read the tree.

### A directive over a shape with no value

> applies to an expression that gives no value

A quantifier that holds no node, such as `option(ref<NUMBER>)`, and a
condition have no value. Put the directive inside,
`option(assign<&T::n>(ref<NUMBER>))`, or make the quantifier a data rule and
assign a call to that rule.

### A node written into a member that takes a value

> applies to an expression that builds a node, but the member takes a value

`assign<&T::name>(option(ref<Item>))` where `name` is a `string_view` and
`Item` builds a node. Give the member a node type, or write the part that
carries the value: `assign<&T::name>(ref<ID>)`.

### A directive over a repetition of nodes

> applies to a repetition that can hold several nodes

Put the directive inside the repetition:
`many(append<&T::m>(ref<Item>), lit<",">)`, not
`append<&T::m>(many(ref<Item>, lit<",">))`.

### A directive over two nodes

> applies to a sequence that can hold two nodes

In `assign<&T::name>(option(ref<Qualifier>) + ref<Name>)`, both parts may
build a node. Create one node that holds both, and assign that node.

### A list item that creates a node

> a create<> or nest<> cannot be the item of a separated list, since it would
> take in the whole list: declare the item as a rule and use ref<> to it

Declare the item as a rule and append it:
`many(append<&List::items>(ref<Item>), lit<",">)`.

### An alternative that creates twice

> this alternative can create two nodes

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

> this alternative returns a node that the text may leave out

In `rule<lit<"let"> + option(ref<Item>)>`, the rule returns the node of
`Item`, which may be absent. Put the node on every path, or create the rule's
own node around the optional part:
`create<Let>(option(assign<&Let::item>(ref<Item>)))`.

### A call after the current node was written

> this alternative calls a rule that builds a node after the current node was
> written, created or nested

An unassigned call makes the called node the
[current node](glossary.md), which works only before anything is written.
Hold the called node with `assign` or `append`, or write the shared part as a
[fragment](grammar.md#reuse-grammar-pieces) in each alternative:

```cpp
static constexpr auto fn_body = kw<"fn"> + assign<&Decl::name>(ref<ID>);
struct Root : rule<option(flag<&Decl::exported>(kw<"export">))
                   + (create<Fn>(fn_body) | create<Var>(var_body))> {};
```

### A member written twice on one path

> this directive writes the member a second time on one path

> this repetition writes the member on every turn

> this directive writes a member that the rule called before it may already
> have written

Write a single-value member at most once on each [path](glossary.md)
through a node, counting the writes of the rules you call. For a list, put
`append` inside the repetition:
`many(append<&Holder::items>(assign<&Part::n>(ref<ID>)))` builds one `Part`
per turn, while `append<&Holder::items>(many(assign<&Part::n>(ref<ID>)))`
writes one `Part`'s `n` on every turn.

### A class that derives from a node class without `Extends`

> this class derives from an AST base without `Extends`: a reference to the
> base would never resolve to it

Write `struct Guest : cursiv::Extends<Person> {}`, not
`struct Guest : Person {}`.

### A directive on the wrong storage

> `=` sets the single value of a member, and this member is a `vector<T>` list

> `+=` appends, so it needs a `vector<T>` list member

Use `assign` for a single value and `append` for a `vector<T>` list.

### A `flag` on a member that is not a bool

> flag<&T::m>: the member must be bool

Make the member a `bool`.

### A directive over a shape with no node

> applies to an expression that can match without a node

The part may hold no node of the member's class: it may be absent, read only
literals, build another class, or have its node taken by an inner directive.
Make the directive optional: `option(assign<&T::m>(ref<Part>))`.

### A nest with nothing in front of it

> this alternative has a nest<> with nothing before it

A nest wraps the current node, so a `create`, a call to an AST rule or a write
must come before it on every path.

### A nest whose member cannot hold the current node

> this nest stores the current node in a member that cannot hold it

The nest's member must accept every class the current node may have there,
including the wrappers of earlier nests.

### A write the current node may not declare

> this directive writes a member that the current node may not have

In `ref<Prim> + assign<&Typed::ty>(ref<ID>)`, `Prim` builds a `Name` or a
`Typed`, and a `Name` has no `ty`. Call a rule that builds only `Typed`, or
write a member that every class `Prim` may build declares.

### An override outside the family of its rule

> this override builds a class outside the type of the rule it overrides

An override must build the rule's class or a class derived from it.

### An override that changes between AST and data rules

> this override changes whether the rule builds a node

Override an AST rule with a body that builds a node, and a data rule with one
that builds none.

### An override that changes a passed value

> this override gives a value of another type than the data rule it
> overrides

An override of a data rule must return the same value type; use `as` to give
it that type. Over a rule that returns a `V`,
`rule<as<V::c>(kw<"c">) | super>` is accepted, but `rule<kw<"c">>` is not.

### A literal the member can never read

> this literal can never convert

When you write a literal, such as `lit<"+">`, or a sequence of literals, such
as `kw<"NOT"> + kw<"IN">` (read as `NOTIN`), into a `bool` or enum member, its
text must be the name of one of the member's values. Use an enumerator's
name, or give the value: `as<Op::add>(lit<"+">)`. Enumerators are found by
name only when their value is between `CURSIV_ENUM_PROBE_MIN` and
`CURSIV_ENUM_PROBE_MAX` (-64 and 512 unless you define them).

### A converter whose result the member cannot take

> no conversion from this value to the member type … `default_converter`

The terminal's converter gives a type the member does not accept.

### A converter function with an unclear input

> as<f>(e): f accepts more than one kind of input

> as<f>(e): f must take the text …, the parts … or no argument

A function given to `as` reads `std::string_view` text, the parts, or nothing,
with an optional `std::pmr::memory_resource*` after them. Give a generic
lambda's parameter a type.

### An `as` inside an `as`

> as<X>(as<Y>(e)) is not allowed: combine the two conversions in one function

Write one function that does both conversions.

### An `as` inside a terminal body

> inside a terminal, as<X>(e) must wrap the whole body or each of its
> alternatives

> inside a terminal, as<V>(e) on one alternative must give a constant value

> inside a terminal, an alternative with its own as<V>(e) may only match words

> inside a terminal, the constants of the alternatives' as<V>(e) have different types

In a terminal, `as` wraps the whole body: `terminal<as<V>(lit<"x"> + lit<"y">)>`.
To give each word its own value, write a data rule:
`rule<as<true>(ikw<"TRUE">) | as<false>(ikw<"FALSE">)>`.

### Data rule alternatives of different types

> the alternatives of this data rule give values of different types

Wrap the whole choice in one `as`, as in
`rule<as<std::string_view>(ref<NUM> | ref<ID>)>`, or make every alternative
return the same type.

### An `as` around a directive or a node

> as<X>(e) converts text, but this e holds a directive

> as<X>(e) converts text, but this e builds an AST node

Move `assign`, `append`, `flag`, `create` and `nest` out of the `as`, and do
not wrap a rule that builds a node.

## AST list storage

```cpp
vector<Person> persons;        // refused: a node is held by pointer
vector<std::string> names;     // refused: elements must be trivially copyable
std::vector<Person*> people;   // refused: use cursiv::vector
```

Each message names the fix.

## Lexical and layout errors

### `regionBoundaryShared`

> the grammar reaches the same token both inside a `skip()` region and
> outside it …

Two alternatives reach the same token, one inside a `skip()` region and one
outside it, so the trivia before that token could follow either setting.
Make the alternatives differ before the region opens, give the region its
own opening token, or end the region on a token that no optional part or
`eof` follows.

### `sectionPointShared`

> the grammar reaches the same token both inside a `section<>` and outside
> it

Two alternatives differ only after the token that opens a section. Make them
differ before the section opens.

### `sectionEndShared`

> the grammar closes a `section<>` on a token that the embedded grammar also
> reads, or that starts one of its tokens

In `section<Expr>(lit<"<">, lit<">">)`, where `Expr` reads `>=`, the `>` of
`>=` could close the section. Close it on a token that no token of the
embedded grammar begins with.

### `sectionIndented`

> the grammar uses indentation and embeds a grammar with `section<>`

Use `section<>` only in a grammar that does not use indentation. The
embedded grammar must not use indentation either: the compiler reports
"a grammar embedded with `section<>` cannot use indentation".

### `indentTwice`

> terminal `OPEN` is a second `indent` or `dedent` terminal

Declare `terminal<indent>` once and `terminal<dedent>` once.

### `indentShape`

> terminal `T` uses `indent` or `dedent` as a part of its body

`indent` and `dedent` must each be a terminal's whole body:
`terminal<indent>`.

### Indentation settings that name no token

> `indent_newline` must name the terminal that ends a line, one that a rule
> uses, not a rule or an unused terminal

Similar messages name `indent_open` and `indent_close`, which may also name
one token per bracket: `lit<"("> | lit<"[">`, not `lit<"("> + lit<"[">`.

### `gapShape`

> the grammar uses an expression in `gap::has` or `gap::is` that they cannot
> test

`gap::has` and `gap::is` take classes, literals, `any`, sequences, choices and
repetitions, never `through`.

### `echoUnmarked`

> terminal `T` uses `same<>` where a `mark<>` with its tag is not set on every
> path before it

A `same<Tag>` needs a `mark<Tag>` before it on every path, in the same
terminal call. A mark in one branch of a choice, or in a loop that may run
zero times, does not count.

### A mark that names no run

> `mark<>` takes one character class or one-character literal, alone or
> repeated: `many(lit<"=">)`, `some(w)`, `repeat<0, 16>(cr<"^() ">)`

`mark<Eq>(lit<"ab">)` is refused: mark one character class or one-character
literal, alone or repeated.

### A capture outside a token

> this expression cannot be used directly in a rule: `mark<>` and `same<>`
> work only inside a terminal or a `through`

`mark` and `same` work inside one token. Use them in a `through` or a
terminal, not directly in a rule:

```cpp
through(lit<"["> + mark<Eq>(many(lit<"=">)) + lit<"[">,
        lit<"]"> + same<Eq> + lit<"]">)
```

### `triviaTied`

> terminal `NOTE` is trivia that matches the same text as another trivia
> terminal

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

> terminal `T` has a recursive call that can never finish a match

`T : terminal<lit<"a"> + ref<T>>` never ends. Add an alternative that ends, or
make the recursive part optional.

### `terminalNullableRepetition`

> terminal `T` repeats an item that may match nothing inside a recursive terminal

Write `many(lit<"b">)`, not `many(option(lit<"b">))`. Terminals that are not
recursive allow it.

### `terminalReadsRule`

> the grammar uses the rule `Pair` inside a terminal or a `through`: these
> read one token, so they cannot call a rule, even a data rule

A terminal reads one token, so it cannot call a rule, even a data rule. Use
the terminals the rule is made of, or call the rule from a rule. A condition
in a rule, such as `&ref<Pair>`, may still name a data rule.

### An expression a terminal cannot read

> this expression cannot be used inside a terminal: a terminal matches one
> token …

A terminal reads one token, so it cannot hold an unordered group, `skip`,
`tight`, a gap condition, `implied` or a directive. Write a group as a choice,
remove `skip`, `tight` and gap conditions or read those parts in a rule, and
put `implied` and directives in the rule that reads the terminal. A condition
that reads a data rule holding an unordered group gets the same message.

### `commitPrefixEmpty`

> rule `Root` has a `commit` whose prefix may match nothing …

A prefix that may read nothing always matches, so the quantifier never
stops. Give the prefix a part it must read:
`commit(option(lit<"x">) + lit<"a">, lit<"b">)`.

### `unorderedTooWide`

> rule `Root` has an unordered group with too many items

A group holds at most eleven items, or six when an item contains `nest`. Use
fewer, or collect them as nodes, `many(append<&T::items>(ref<Item>))`, and
check uniqueness in validation.

## Advanced token restrictions

These rare errors concern negative guards in terminals, such as `!kw<"if">`,
which excludes the word `if`. A character of the word boundary after the word
lets the guard pass: `!kw<"if">` still allows `iffy`.

### `wordPastMatch`

> terminal `HASH` may end a match before the end of `'abc'`, which a guard
> excludes after the start of the terminal

In `lit<"#"> | lit<"##", !lit<"abc">>`, the match `##` ends before `abc` is
read. Give the guarded literal a terminal of its own, or make the token read
the whole word before it can end. A guard on a character class, such as
`!uc::Lo`, gets this message when a match may end inside one of its
multi-byte characters.

### `wordRefusedInLoop`

> terminal `STARS` has a guard inside a repetition, and a turn of that
> repetition may read nothing after the guard

Make each turn of the loop read something after the guard:
`!lit<"/"> + any`.

### `rescuedRefusalPastMatch`

> terminal `W` may end a match exactly where a word its guard excludes ends,
> after the start of the terminal, and `_` after that word lets the guard pass

Put the guard at the start of the terminal, or after literals and character
classes only, or let the token read the characters that allow the longer
word.

### `refusalIntoRelay`

> terminal `CMT` has a guard whose excluded word reaches into the text between
> a `mark<>` and its `same<>`

In a nested run, the guard may test only the first character of that text,
and not as a keyword.

### `rescueReadsPast`

> terminal `NAME` excludes a word with a keyword guard, then may read after it
> a character outside `word_boundary`

Let the word boundary hold every character the terminal reads after the word.

### `boundaryDisagrees`

> terminal `T` reaches the same match through two guards that exclude
> different characters after it

LR only; LL accepts this terminal.

### `boundaryMisses`

> terminal `NAME` excludes `"in"` at its start, and can read `\` at the start
> of a word and again after `"in"`, but `\` is not in the word boundary

Let the boundary hold the character: `cr<R"(a-zA-Z0-9_\\)">`.

## Warnings

A warning does not stop the build. Only a
[repeated alternative](#an-alternative-that-repeats-an-earlier-one) changes the grammar: the generator
drops that alternative.

### An alternative that repeats an earlier one

> warning: rule `Root`: branch 2 of 2 `b=ID` matches the same tokens as
> branch 1, so it is never chosen and is dropped …

> warning: rule `Root`: `(b=ID)?` matches the same tokens as the earlier
> `(a=ID)?`, so it is never chosen in place of that item, and that choice is
> dropped …

A choice takes the first alternative written, so an alternative that reads the
same tokens as an earlier one never wins, whatever it writes. The same holds
for an optional item that reads the same tokens as an earlier optional item.
Merge the two, or reorder them so that the one you mean comes first.

### A run nested in a run

> warning: rule `Root`: `(X+)*` repeats a repetition, so a run of items can be
> split into groups in several ways and the parser picks one …

`many(some(ref<X>))`, even through a rule or a choice, groups the input in
several ways, and [parse selection](grammar.md#choosing-between-parses) picks
one. Write `many(ref<X>)`, with `append` inside to collect one child per item.

### Two runs side by side

> warning: rule `Root` has two repetitions of `NL` in a row: a run of them can
> be split between the two in several ways -- write one repetition

LR only. Write one repetition. The warning also applies when only an option
that may be absent separates the two runs:
`many(ref<X>) + option(ref<Y>) + some(ref<X>)`. Move the first run into the
option: `option(many(ref<X>) + ref<Y>) + some(ref<X>)`.

### An unbounded run in a condition

> warning: rule `Arrow` has a condition that matches `('-')+` with no length
> limit: code completion then has to re-read every token before the cursor …

A condition with no maximum may read to the end of the document, which slows
completion; a run inside another repetition can also make parsing quadratic,
and the warning then says so. Give the run a maximum,
`!(repeat<1, 8>(lit<"-">) + lit<">">)`, or read it as one token,
`!ref<ARROW>` with `ARROW : terminal<some(lit<"-">) + lit<">">>`. A terminal
reads adjacent text only: in `- ->`, only `->` is then an arrow.

### A branch past `eof`

> warning: rule `Para`: branch 2 of 2 `ADORN !. EOL` can never match -- `EOL`
> comes after `eof`, and nothing can follow the end of input

The message spells `eof` as `!.`. Remove what follows `eof`, or let either one
end the branch: `ref<ADORN> + (ref<EOL> | eof)`. The warning also names an
option after `eof`, and an `eof` branch that a token follows.

### A flag that may read nothing

> warning: rule `Root`: `on?=("on"?)` flags an operand that may match nothing,
> so it always sets the member -- flag a token that must be there, and make the
> flag optional

Write `option(flag<&Item::on>(kw<"on">))`, not
`flag<&Item::on>(option(kw<"on">))`, and initialize the member to `false`.

### A word literal with no guard

> warning: rule `Root`: `'begin'` is a literal with no guard that ends in a
> word character, so it also matches the start of a longer word, such as
> `beginx` -- write `kw<"begin">`, or give the literal a guard
> (`lit<"begin", G>`) if that is intended

Literals in terminals and conditions get no warning.

### A lexical check that cannot conclude

> warning: terminals `LONG` and `TAG` are trivia, but the generator could not
> check that they never match the same text with the same length and
> category …

The [`sectionEndShared`](#sectionendshared) and [`triviaTied`](#triviatied)
checks cannot always look inside a terminal that uses a capture, a call, a
nested run or a condition. They then warn instead of refusing.

## Where the engines differ

- **LR reads, LL refuses** with [`leftRecursion`](#leftrecursion): some
  indirect left recursion, a left-recursive call inside `many` or `some`, and
  recursive cycles past LL's size limit.
- **LL reads, LR refuses**: one terminal match behind two guards of different
  classes ([`boundaryDisagrees`](#boundarydisagrees)), and more than 32
  implied tokens ([`gapTooMany`](#grammar-size-limits)).
- **Different codes**: [`unitProductionCycle`](#unitproductioncycle) and
  [`hiddenLeftRecursion`](#hiddenleftrecursion) in LR are `leftRecursion` in
  LL, and the [size limits](#grammar-size-limits) differ.
- Only LR gives the [two runs side by side](#two-runs-side-by-side) warning.

## Grammar size limits

These codes mean that the generated parser would be too large. The counts
include the rules and alternatives that the generator adds to yours.

| Code | The grammar… |
|---|---|
| `namePool` | declares too many distinct names |
| `tooManyTerminals` | declares more than 32 767 terminals |
| `tooManyRules` | declares more than 32 767 rules, generated ones included |
| `tooManyConfigurations` | uses more than 255 trivia settings: its own, one per distinct `skip()` and one per `section<>` |
| `gapTooMany` | states more than 255 gap conditions, or (LR only) more than 32 implied tokens |
| `gapLeafPool` | declares too many distinct condition expressions |
| `tokenGraphTooLarge` | has tokens too complex to generate, or a repetition bound above 4 294 967 294 |
| `tooManyPositions` | exceeds LL's size limit |
| `tooManyProductions` | has too many alternatives for LR |
| `productionTooWide` | has an alternative too long for LR |
| `tableTooLarge` | exceeds LR's size limit |

Counted repetitions and unordered groups grow fast: in a rule,
`repeat<Min, Max>` costs about `Max` copies of its item. Use an unbounded
repetition and check the count in validation, reduce alternatives, factor
shared expressions, or split the grammar into separate languages. The same
fixes apply to messages without a code, such as
`the grammar is too large for the generated parser: …` or, in LL,
``rule `Root` repeats a `nest` more than 64 times in a row``.
