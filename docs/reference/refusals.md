# Grammar errors and fixes {#refusals}

A **refusal** is an error in the grammar detected before a parser is
generated. Find the reported code or message below to understand the cause
and its correction. Quoted messages are excerpts from the generator or C++
compiler; the surrounding text explains how to fix them.

LL and LR share diagnostic codes. Some limits differ between the engines;
see [Where the engines differ](#where-the-engines-differ). An error reported
by a C++ `static_assert` stops compilation of the generator itself.

## Common problems

| Code or message | Cause | Correction |
|---|---|---|
| [`nullableRepetition`](#nullablerepetition) | An item repeated more than once may consume nothing. | Repeat a consuming item; put optionality in the repetition's bounds. |
| [`terminalEmpty`](#terminalempty) | A token or trivia terminal may consume nothing. | Make the terminal consume input; put optionality on its call in the rule. |
| [`skippedTerminal`](#skippedterminal) | A rule explicitly reads a terminal that is trivia there. | Remove the call, or change the trivia scope with `tight` or `skip`. |
| [`twinProductions`](#twinproductions) | Indistinguishable alternatives write different members. | Give the alternatives distinct syntax or merge them. |
| [`selfBoundedArm`](#selfboundedarm) | A recursive operator rule leaves grouping unspecified. | Use `infix` or separate precedence levels. |
| [`leftRecursion`](#leftrecursion) | LL cannot transform this recursive cycle. | Check the cycle's structure or use `infix` for binary operators. |
| [A member written twice](#a-member-written-twice-on-one-path) | One node's scalar member is assigned more than once. | Write it once, or use `append` with a list member. |
| [A repetition of nodes](#a-directive-over-a-repetition-of-nodes) | One directive receives several nodes. | Put `append` inside the repetition. |
| [A node may not exist](#an-alternative-whose-node-may-not-exist) | An AST rule can finish without returning a node. | Create a container and assign the optional child inside it. |
| [No value](#a-directive-over-a-shape-with-no-value) | A directive wraps a quantifier instead of its value-producing item. | Put the directive inside the quantifier. |

Use the diagnostic code to jump to its explanation, or choose a category:

- [Rule errors](#refused-against-a-rule): repetition, conditions and operators.
- [C++ type errors](#refused-by-the-compiler): AST members and directives.
- [Lexical and layout errors](#refused-by-the-lowering): tokens, comments and indentation.
- [Advanced token restrictions](#advanced-token-restrictions): guards in complex terminals.
- [Engine differences](#where-the-engines-differ) and [size limits](#counts-a-program-cannot-hold).

## Rule errors {#refused-against-a-rule}

The error report names the rule and shows its original and generated forms.
For a recursive cycle, it also lists the rules involved:

```
refused (unitProductionCycle)
cursiv: rule `A` stands on a cycle of reductions that read no text …
    A returns Node: B | {Node} name=ID;
    A -> B
    A -> ID
    cycle: `A` -> `B` -> `A`
```

### `nullableRepetition`

> rule `Root` repeats an item that may match nothing

An item repeated more than once must consume input. Otherwise the parser
could add empty items without advancing. This check also follows rule calls
and unordered groups.

Move optionality into the repetition's bounds:

| Refused | Use instead |
|---|---|
| `many(option(x))` | `many(x)` |
| `repeat<2, 3>(option(x))` | `repeat<0, 3>(x)` |

A repetition with a maximum of one is an option; its item may match nothing.

### `ruleWithoutProduction`

> rule `Tail` derives nothing: every reading of it turned out impossible, so
> whoever calls it can never return

No alternative can succeed. Check for a required token after `eof`, or a
recursive rule with no terminating alternative. Keep at least one possible
match.

### `twinProductions`

> rule `Root` holds two alternatives that spell the same symbols

Two alternatives match the same token sequence but write different members.
The input cannot distinguish them. Give them different syntax or merge them
into one alternative. Two optional items matching the same symbol can cause
the same problem, regardless of how many other optional items surround them.

### `twoEndMarks`

> rule `Root` asserts end of input twice in one choice

Remove the repeated `eof` assertion from that alternative.

### `optionalEndMark`

> rule `Root` makes an end-of-input assertion optional

`option(eof)` imposes no restriction when absent. Require `eof` directly,
or use an alternative such as `eof | ref<Other>` when either is meaningful.

### `optionalCondition`

> rule `Root` makes a condition optional

An optional condition imposes no restriction when absent. Require the
condition directly, or put `option` around the expression it guards.

### `directiveOnNoPosition`

> rule `Root` writes a member from `eof` alone, or from nothing

`flag<&Doc::last>(eof)`: `eof` consumes nothing. Put the directive on a
consuming part, or on a choice one branch of which is `eof`.

### `skippedTerminal`

> rule `Root` names a terminal its reading skips as trivia

A rule cannot explicitly match a terminal currently listed in `ignore` or
`hidden`: the lexer skips it first. Remove the call, or use `tight(e)` or a
local `skip()` configuration that treats this terminal as a regular token.
The check uses the trivia configuration active at that call.

An operator's own rule is read the same way: `left(ref<Plus>)` where `Plus`
reads the skipped `WS` is refused.

### `docConflict`

> rule `Root` documents one word twice with different prose

`kw<"if">.doc<"a">()` in one place and `kw<"if">.doc<"b">()` in another spell
one token, and a token has one hover text. Keep one text, or document the word
once.

### `precedenceClash`

> rule `Sum` names the same operator text on two levels

Two operators may open on one token and stand on two levels, or in two
tables, where the text after it tells them apart: `>` against `>` `>` (a
shift written as two tokens, so that `List<List<int>>` still lexes), `not in`
against `not like`. Each binds at its own level and grouping, as if it had a
token of its own.

The same text on two levels cannot be told apart by anything: `*` on two
levels, `^` grouping left in one table and right in another, two rules that
both spell `not in`. A condition on an operator is no difference, only its
tokens are compared, and an operator that repeats an item is compared by the
token it opens on alone. Give each text one level and one grouping, across
every table that reads it.

### `infixOperatorNotText`

> rule `Sum` names as an operator a rule that builds a node

An operator must produce a value, not an AST node. Use a literal, terminal,
data rule or sequence of these, with a converter if the operator member is
not text. A sequence containing an AST-producing call is also refused.

### `tooManyPrecedences`

> rule `Expr` declares more than 255 precedence levels

Use at most 255 levels.

### `unitProductionCycle`

> rule `A` stands on a cycle of reductions that read no text (`A -> B`,
> `B -> A`, or `A -> A B` where `B` may match nothing)

Reported by LR. The cycle can return to its start without consuming input.
Break the cycle with a consuming expression or remove the redundant call.
LL reports the same grammar as [`leftRecursion`](#leftrecursion).

### `hiddenLeftRecursion`

> rule `Type` reaches itself at its left edge behind an item that may match
> nothing (`A -> B A x` where `B` can be empty)

Reported by LR; the LL engine refuses the same grammar as
[`leftRecursion`](#leftrecursion). Before the first token the parser cannot
know how many empty `B`s to read. Make the item consume, or move it after the
recursive call: `A -> A x` is plain left recursion and is fine.

More than seven optional items before the recursive call can also trigger
this refusal. Keep seven or fewer, group some behind a consuming item, or
put the recursive call first.

### `leftRecursion`

> rule `Expr` reaches itself before matching anything in a way the LL descent
> cannot fold

LL only. This includes LR's
[`unitProductionCycle`](#unitproductioncycle) and
[`hiddenLeftRecursion`](#hiddenleftrecursion), a call behind a rule that may
match nothing -- or behind `eof`, which reads nothing either -- among them.
Spell a chain of binary operators as `infix<>`.

For indirect recursion, a common fix is to add a cycle alternative that only
calls another rule in that cycle. Another is to inline one rule so that the
recursion becomes direct. When alternatives compete for the same prefix,
their order also matters.

??? note "Detailed LL restrictions for indirect recursion"

    Some indirect cycles work with LR but require a rewrite for LL. The message
    names the cycle. Check these two restrictions:

    **A call-only alternative.** At least one rule in the cycle needs an
    alternative consisting only of a call to another rule in that cycle:

    ```text
    A := B "x" | "a" "x"       B := A "y" | "a"       refused: no call-only alternative
    A := B | "a" "x"           B := A "x" | "a"       read
    ```

    Add such an alternative, or inline one rule into the other to make the
    recursion direct. Inlining must preserve the original AST construction.

    **Competing recursive alternatives.** When a rule in the cycle also calls
    itself directly, put that alternative before an indirect recursive
    alternative with a competing prefix:

    ```text
    A := B                     B := A "y" | B "y" | "b"
    ```

    This is refused by LL. Here the direct alternative `B "y"` is redundant;
    removing it preserves the selected trees. Where the alternatives differ
    after their shared prefix, put the direct alternative first:

    ```text
    B := A "y" "z" | B "y" "w" | "b"       refused
    B := B "y" "w" | A "y" "z" | "b"       read, with the same trees
    ```

    Distinct first literals can avoid this conflict: `"y"` and `"z"`, or
    `"ya"` and `"yb"`. A terminal containing just one literal counts too.
    Identical spellings, prefixes such as `"y"` and `"yy"`, or a difference
    only in a later token do not distinguish the alternatives for this check.
    Neither does a character class or rule call in place of the literal.
    Operators at distinct levels of one `infix` are already distinguished.

    Larger cycles can have the same ordering conflict even without direct
    self-calls. Factor them into explicit precedence levels, inline a rule, or
    use LR when the original recursive structure is required.

    LL also limits a recursive cycle to 100 rules, each with at most 16
    alternatives, and a path of leading calls to 48 rules. Simplify the cycle
    or use LR if one of these limits is reached.

### `selfBoundedArm`

> rule `E`: the arm `E '+' E` begins and ends with `E`, which says nothing of
> how its operands nest -- declare the operators with infix<>, or give each
> level its own rule (`E := E + T | T`)

An alternative begins and ends with a call to its own rule, ignoring
conditions. For example, `E := E + E | a` does not say whether `a+a+a`
groups as `(a+a)+a` or `a+(a+a)`.

The same restriction applies to `E := E E | a` and
`E := E "?" E ":" E | a`, regardless of the tokens between recursive calls,
alternative order or surrounding `create`, `nest`, `assign` or `append`.
Both engines check AST and data rules. An `infix` declaration already states
its grouping and is not subject to this check.

Declare the operators with [`infix`](grammar.md#operator-precedence), or give
each level its own rule: `E := E + T | T`, `T := T * F | F`. An alternative that ends
in a token (`E := E "[" E "]" | a`) or begins with one (`E := "-" E | a`) is
no such alternative.

## C++ type errors {#refused-by-the-compiler}

### A grammar named with `ref<>`

> `ref<G>` names a grammar: read it with `section<G>(open, close)`

`ref<Json>` where `Json` is a grammar: nothing says where Json's text ends.
Write `section<Json>(lit<"<">, lit<">">)`. A grammar that states indentation,
or whose entry is a terminal, is refused where it is embedded too.

### A trivia list that names a rule

> `ignore`, `hidden` and `skip()` list terminals only: trivia are tokens the
> lexer skips, never rules the parser reads

`using ignore = list<WS, Blank>;` where `Blank` is a `rule<>`: make it a
`terminal<>`.

### A terminal in both trivia lists

> a terminal belongs to at most one of `ignore` and `hidden`: trivia are
> dropped or kept in the tree, never both

`using ignore = WS; using hidden = WS;`: list `WS` in the one that says what
becomes of it. The same holds for the lists of a `skip()` and of an embedded
grammar.

### A commit outside a quantifier

> commit(prefix, body) stands only as the direct item of option, many, some or
> repeat

`commit(a, b) + c`, `commit(a, b) | c` and a rule whose whole body is a commit
are refused: a commit is what a quantifier guards its exits with. Write
`many(commit(a, b))`.

### A commit inside a terminal

> a commit stands in a rule, not in a terminal: a terminal reads adjacent bytes
> and has no condition to stop on

`terminal<many(commit(lit<"a">, lit<"b">))>` is refused. Use the plain
repetition forms inside terminals.

### An implied token that is no unguarded literal

> `implied(tok, cond)` lays a token of no width, so `tok` is an unguarded
> literal or a terminal whose body is one

`implied(ref<NEWLINE>, eof)` where `NEWLINE` reads a line ending and what
follows it, and `implied(kw<"end">, eof)`, whose word boundary has no byte to
look at: a token laid with no width has only its spelling. Write
`ref<NEWLINE> | eof` where the token may be absent, or imply a literal.

### A condition over a rule that builds a node

> A rule that builds a node is parsed, not read: no condition asks one

`&ref<Name>` or `!ref<Name>` where `Name` creates a node: a condition reads
text and builds nothing. Ask the terminals the rule starts with, or a data
rule that spells them.

### A section in a condition or a terminal

> `section<G>(open, close)` stands in a rule, not in a condition: a condition
> reads text and never parses a grammar -- ask the token that opens the section

> `section<G>(open, close)` stands in a rule, not in a terminal: a terminal is
> one token and a section parses a grammar

`&section<Json>(lit<"<">, lit<">">)` and `terminal<lit<"a"> +
section<Json>(…)>` are refused. Ask `&lit<"<">` in the condition; read the
section in a rule.

### A condition over a rule that reaches itself

> a condition reads a data rule that reaches itself

`!ref<Call>` where `Call : rule<ref<ID> + option(lit<"("> + ref<Call> +
lit<")">)>`: recursive data rules are not supported in predicates. Ask what
the rule starts with instead.

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

`assign<&T::n>(option(ref<NUMBER>))`: a quantifier alone (`option`, `many`,
`some`, `repeat`) over no node has no value, so the member would be written
from nothing, and so would a condition alone. Put the directive inside the
quantifier, `option(assign<&T::n>(ref<NUMBER>))`, or name the quantifier as a
data rule and write that rule.

### A node written into a member that takes a value

> stands over a shape that builds a node, and the member takes a value

`assign<&T::name>(lit<"("> + ref<Item> + lit<")">)` and
`assign<&T::name>(option(ref<Item>))`, where `name` is a `string_view` and
`Item` builds a node: the node would be held by nothing. Give the member a
node type, or write the part that carries the value,
`assign<&T::name>(ref<ID>)`.

### A directive over a repetition of nodes

> stands over a repetition that can hold SEVERAL nodes

`append<&T::m>(many(ref<Item>, lit<",">))`: each write takes one node, and the
repetition holds as many as the text spells. Put the directive under the
repetition, `many(append<&T::m>(ref<Item>), lit<",">)`.

### A directive over two nodes

> stands over a sequence that can hold TWO nodes

`assign<&T::name>(option(ref<Qualifier>) + ref<Name>)`: the member takes one
node, and the sequence holds two. Create the node that holds both,
`create<Q>(assign<&Q::qualifier>(ref<Qualifier>) + assign<&Q::id>(ref<Name>))`,
and write that.

### An alternative that creates twice

> this alternative can create twice

`rule<some(ref<Item>)>`: each turn is a node, and a rule hands over one. Give
the repetition a node that holds it,
`create<List>(some(append<&List::items>(ref<Item>)))`.

A rule cannot combine a standalone `create` with an unassigned AST-producing
call, in either order, or combine two unassigned AST-producing calls. It
would return one node and lose the other. For example,
`create<Holder>(assign<&Holder::first>(ref<Item>) + ref<Item>)` leaves the
second `Item` unstored. Use `assign<&Holder::second>(ref<Item>)` instead.

An AST-producing call inside `nest` must also be stored in a member. Wrapping
it in `flag` does not store it:
`create<Holder>(flag<&Holder::on>(ref<Item>))` is still refused.
A `create` after a `nest` is also refused because the wrapper is already the
current node. Put `create` before the nest, or select the wrapper's class
with `nest<MarkedBox, &Box::inner>(…)`.

An unordered group is also refused when two items create nodes, or one
creates a node and another nests it. For example,
`create<A>(lit<"a">) & create<B>(lit<"b">)` cannot return both nodes. Create
one container before the group and let the items fill its members:
`create<Node> + (assign<&Node::a>(lit<"a">) & assign<&Node::b>(lit<"b">))`.

### An alternative whose node may not exist

> this alternative hands over a node the text may leave out

`rule<option(ref<Item>)>`, `rule<lit<"let"> + option(ref<Item>)>` and
`rule<lit<"let"> + (ref<Item> | lit<"nothing">)>`: the node the rule hands over
is the one `Item` creates, and a text that skips it leaves the rule with none.
Put the node on every path, or create a node of the rule's own around the
optional part and let it write into it,
`create<Let>(option(assign<&Let::item>(ref<Item>)))`.

### A call after the node in hand was written

> this alternative calls a rule that builds a node after the node in hand was
> written, created or nested

`assign<&Decl::name>(ref<ID>) + ref<Body>`,
`option(flag<&Decl::exported>(kw<"export">)) + ref<Body>` and
`assign<&Decl::name>(ref<ID>) + flag<&Decl::on>(lit<"("> + ref<Body> +
lit<")">)`: a call adopts the node it returns while the current node is still
empty, and nothing merges into it. Hold the called node with `assign` or
`append`, or write the shared part as a fragment, an expression whose
directives write the current node, and spell it in each alternative that needs
it:

```cpp
static constexpr auto fn_body = kw<"fn"> + assign<&Decl::name>(ref<ID>);
struct Root
    : rule<option(flag<&Decl::exported>(kw<"export">)) +
           (create<Fn>(fn_body) | create<Var>(var_body))> {};
```

### A member written twice on one path

> this directive writes the member a second time on one path
> this repetition writes the member on every turn

Assign a single-valued member once on each possible path through a node.
For a list, use `append` inside the repetition. A `nest` creates a wrapper;
the operands of `assign` and `append` build their values separately:
`append<&Holder::items>(many(assign<&Part::n>(ref<ID>)))` writes the one
`Part`'s `n` on every turn, where `many(append<&Holder::items>(assign<&Part::n>(
ref<ID>)))` builds a `Part` per turn.

> this directive writes a member the node of the rule called before it may have
> written already

`ref<ItemRule> + assign<&Item::name>(ref<ID>)` where `ItemRule` writes `name`: a
call adopts its node, and a write after it lands there. Write the member in one
place.

### A class that derives from a node class without `Extends`

> this class derives from an AST base without `Extends`: a reference to the
> base would never resolve to it

`struct Guest : Person {}` is C++ inheritance, and a reference resolves through
the bases a class declares: `struct Guest : cursiv::Extends<Person> {}`.

### A directive on the wrong storage

> `=` names the ONE value a member takes, and this member is a `vector<T>` list
> `+=` accumulates, so it names a member the grammar can accumulate INTO

Use `assign` for a single-valued member and `append` for a `vector<T>` list.
For example, replace `assign<&T::items>(e)` with `append<&T::items>(e)` when
`items` is a list. Use `assign<&T::name>(e)` when `name` is a scalar member.

### A `flag` on a member that is not a bool

> flag<&T::m>: the member must be bool

`flag<&T::m>(e)` sets `m` to true where `e` matched, so `m` is a `bool`.

### A directive over a shape with no node

> stands over a shape that can match with NO node

A member that takes a node, over a shape where a match may hold none of its
family: a quantifier of literals only, a part that may be absent, a node of
another class, or one a directive inside already takes. Make the directive's
own part optional, `option(assign<&T::m>(ref<Part>))`.

### A nest with nothing in front of it

> this alternative has a nest<> with nothing in front of it

`many(nest<Wrapper, &Wrapper::inner>(...))`, `lit<"a"> + many(nest<...>(...))`
and `option(assign<&Wrapper::name>(ref<ID>)) + nest<...>(...)`: a nest buries
the current node under its wrapper, so a `create`, a call to a rule that builds
a node or a write has to come first on every path.

### A nest whose member cannot hold the node in hand

> this nest buries the node in hand in a member that cannot hold it

`create<Other>(...) + option(nest<Pair, &Pair::left>(...))` fails when
`Pair::left` cannot hold an `Other`. The nested member must accept every
possible class of the preceding node, including wrappers created by earlier
nests. Choose a compatible member type or limit the preceding alternatives.

### A write the node in hand may not declare

> this directive writes a member of a class the node in hand may not be

`ref<Prim> + lit<":"> + assign<&Typed::ty>(ref<ID>)` where `Prim` builds a
`Name` or a `Typed`: the write lands on the adopted node, and a `Name` has no
`ty`. Adopt the subclass alone, or write a member every adopted class declares.

### An override outside the family of its rule

> this override hands back a class outside the type of the rule it stands over

`override<Base::Item> : rule<create<Other>(...) | super>` where `Other` does not
derive from the `Item` the base's callers read. An override builds the rule's
class or one derived from it -- the node it creates, or the wrapper of its last
nest.

### An override that changes between AST and data rules {#an-override-that-changes-a-rules-genre}

> an override restates the rule's genre and this one changes it

An AST rule overridden by a body that builds no node (`rule<ref<ID>>` over a
rule that creates an `Item`), or the reverse: callers of the base would still
read the rule as the node's type.

### A literal the member can never read

> this literal can never convert

A bare literal written into a `bool` or an enum member is the
whole payload, so its spelling is known: `lit<"+">` as an infix operator into
an `Op` that declares no `+` would be a diagnostic on every match. So is a
sequence of literals and keywords, as an infix operator or under `assign` or
`append`, whose spelling is its parts joined: `kw<"NOT"> + kw<"IN">` into an
`Op` that declares no `NOTIN`. Spell an enumerator, or name a rule that states the
value (`value<Op::add>`). An enumerator is read by name only when its value
lies in `CURSIV_ENUM_PROBE_MIN` to `CURSIV_ENUM_PROBE_MAX` (-64 to 512 unless
defined).

### A converter whose result the member cannot take

> no conversion from this payload to the member type … `default_converter`

The terminal's converter announces a type the member does not accept.

## AST list storage {#and-one-more-the-vocabulary-carries}

```cpp
vector<Person> persons;        // refused: a node is held by pointer
vector<std::string> names;     // refused: elements must be trivially copyable
std::vector<Person*> people;   // refused: use cursiv::vector
```

All three are `static_assert`s that name the fix.

## Lexical and layout errors {#refused-by-the-lowering}

A refusal may name the grammar or terminal concerned without a code in front:

```
refused: the grammar reaches one point through a `skip()` region …
refused: terminal `HASH` may end a match short of 'abc', a word it refuses past its head …
```

### `regionBoundaryShared`

> the grammar reaches one point through a `skip()` region and outside it at
> once: the point that reads the next token cannot read the trivia in front of
> it two ways

Two alternatives that part only inside the region, or a region that may go on
or end where an optional part follows, `eof` or the end of the document
included. Part the alternatives before the region opens, give the region an
opening token of its own, or end it on one no optional part follows.

### `sectionPointShared`

> the grammar reaches one point inside a `section<>` and outside it at once

Two alternatives that part only after the token opening a section: the point
behind that token would read the trivia in front of the next one both as the
embedded grammar's and as the caller's. Part the alternatives before the
section opens.

### `sectionEndShared`

> the grammar closes a `section<>` on a token the embedded grammar reads, or on
> the start of one

`section<Expr>(lit<"<">, lit<">">)` where `Expr` reads `>=`: in `<a >= b>` the
section would end where the embedded grammar's lexer says, not where the text
does. Close the section on a token none of the embedded grammar's begins
with.

### `sectionIndented`

> the grammar reads its lines by indentation and embeds a grammar with
> `section<>`

The section's lines would be measured as the document's: embed it in a
grammar that states no indentation.

### `indentTwice`

> terminal `OPEN` declares two indent terminals, or two dedent terminals

The indentation lays one token where a line opens a block and one where it
closes it: declare `terminal<indent>` once and `terminal<dedent>` once.

### `indentShape`

> terminal `T` spells `indent` or `dedent` inside a body

Each is a terminal's whole body, `terminal<indent>`.

### `gapShape`

> the grammar asks a condition of an expression the matcher does not read

`gap::has(through(lit<"/*">, lit<"*/">))`: `gap::has` and `gap::is` take
classes, literals, `any`, sequences, choices and repetitions, never a `through`
run.

### `echoUnmarked`

> terminal `T` echoes a mark<> not set in its scope

An echo needs a `mark<Tag>` before it on every path of its scope: a mark in
one alternative of a choice, or in a loop that may not turn, binds nothing after it.
A terminal call starts a local capture scope and restores its caller's
captures when it returns. An echo cannot read a tag defined only inside
another terminal call. A lookahead inherits the captures available where
it begins and may define local marks of its own; its writes are discarded
on return. Set the tag in a scope the echo can read.

### `triviaTied`

> terminal `NOTE` is trivia that reads the same text as another trivia terminal

Two trivia terminals that match the same text at equal length and rank, one
of them in `hidden`, leave the tree to the order they are declared in: a
comment would be kept or dropped by where the grammar lists it. Put both in
`ignore`, where the choice shows in no tree, or make them read different
texts. A branch that spells a literal outranks one that reads a pattern, so a
trivia terminal whose branch spells a literal and one that reads a pattern are
no tie on the text that branch spells; a guard or a condition adds no rank.

### `terminalEmpty`

> terminal `T` matches the empty string -- give its body a part it must read

A token reads at least one byte: `terminal<many(d)>` and
`terminal<many(!lit<"*/"> + any)>` may both read nothing. Write `some(d)`, or
let the rule that reads the terminal make it optional. A terminal only a
condition names may match nothing.

### `terminalCycle`

> terminal `T` calls itself without consuming input

A recursive terminal must consume input before returning to the same rule.
`T : terminal<ref<T> + lit<"a"> | lit<"b">>` calls itself at its starting
position, as does `option(lit<"a">) + ref<T>` when the option is empty.
Lookahead consumes nothing, so `!ref<T> + any` also forms an invalid cycle.
By contrast, `lit<"a"> + option(ref<T>)` consumes a byte before its recursive
call and has a finite base case.

### `terminalUnproductive`

> terminal `T` has a recursive call that cannot return a finite match

Every recursive call must have a finite successful derivation. For example,
`T : terminal<lit<"a"> + ref<T>>` always calls itself again and never returns.
Add a terminating alternative or make the recursive continuation optional.
An alternative elsewhere in the terminal does not permit a separate
recursive branch whose calls can never return.

### `terminalNullableRepetition`

> terminal `T` repeats an item that may match nothing inside a recursive terminal

An item repeated more than once in a recursive terminal must consume input.
For example, use `many(lit<"b">)` instead of `many(option(lit<"b">))`.
An optional item remains valid outside a repetition. This restriction applies
to recursive terminals; non-recursive terminals allow nullable repeated items
and select the longest match.

### `terminalReadsRule`

> the grammar reads the rule `Pair`: a rule is read with the skipper between
> its parts, a phrase of tokens and never the bytes of one

`PAIR : terminal<ref<ID> + lit<":"> + ref<Pair>>`, where `Pair` is a rule that
builds a node or a data rule such as `rule<ref<ID> + lit<".">  + ref<ID>>`: a
terminal is the bytes of one token, and a rule skips trivia between its
elements. Name the terminals it is made of, or read the rule from a rule. A
rule's condition (`&ref<Pair>`, `!ref<Pair>`) may still ask a data rule: it is
read there as a phrase.

### `commitPrefixEmpty`

> rule `Root` commits on a prefix that may read nothing: its quantifier stops
> where the prefix cannot be read, and a prefix that reads nothing is always
> there -- give the prefix a part it must read

`many(commit(option(lit<"a">), lit<"b">))`: the prefix is read everywhere, so
the quantifier could never stop. A prefix with an optional part is fine when
something else in it must be read: `commit(option(lit<"x">) + lit<"a">,
lit<"b">)`.

### `unorderedTooWide`

> rule `Root` holds an unordered group of too many items

Groups support at most eleven items, or six when an item contains `nest`.
Group fewer items, or use a repetition of a choice and check uniqueness
during validation.

## Advanced token restrictions

These diagnostics concern guards inside complex terminals. Start with the
named terminal and use the correction below; ordinary identifiers, literals
and comments do not require these details.

Here, a *refused word* is text excluded by a negative guard such as
`!lit<"abc">` or `!kw<"if">`. A word-boundary class can allow the text again
when it continues a longer word: `!kw<"if">` allows `iffy`. Diagnostics call
that class a *rescue class*.

### `wordPastMatch`

> terminal `HASH` may end a match short of `'abc'`, a word it refuses past
> its head

`lit<"#"> | lit<"##", !lit<"abc">>`: the match `##` ends before `abc` is
read. Give the guarded literal a terminal of its own, where the word stands
behind literals alone, or let the body read the word whole before it may
stop.

A class of scalars is such a word where a match may end inside one of
its scalars:

> terminal `NUM` may end a match short of `\p{Lo}`, a word it refuses past
> its head

`some(cr<"0-9">) + !uc::Lo + option(lit<"\xe4">)`: the match may end one
byte into a scalar the class holds, and the rest of that scalar is text past
the token.

### `wordRefusedInLoop`

> terminal `STARS` refuses a word inside a repetition that may come back to a
> refusal without reading a byte

`lit<"#"> + many(lit<"*"> | !lit<"/">)`: the turn `!lit<"/">` reads nothing,
so the repetition meets the refusal again where it stands. Give the turn a
part it must read after the refusal, as in `!lit<"/"> + any`.

### `rescuedRefusalPastMatch`

> terminal `W` ends a match exactly where the word it refuses ends, past its
> head, and `_` rescues that word

The reported guard cannot be combined with that word boundary at the token's
end. Put the guard at the start of the terminal, or after a prefix consisting
only of literals and character classes. Alternatively, make the token consume
the characters that allow the longer word.

### `refusalIntoRelay`

> terminal `CMT` refuses a word that runs into a relay's content

In a nested run, restrict this guard to the content's first byte, without a
rescue class.

### `rescueReadsPast`

> terminal `NAME` reads on past a word it refuses over a character its word
> boundary does not hold

Let the word boundary include every character the terminal reads after the
word.

### `boundaryDisagrees`

> terminal `T` reaches one match through two refusals of different classes

LR only. LL accepts this shape.

### `boundaryMisses`

> terminal `NAME` refuses `"in"` at its head, and reads `\` where a word of
> its own begins and again past `"in"`, yet the class that rescues `"in"`
> leaves `\` out

`!kw<"in"> + (cr<"a-z"> | Esc) + many(cr<"a-z"> | Esc)` under the default
boundary: `inA` would read as `in` and a second name. Let the boundary
hold the character, `cr<R"(a-zA-Z0-9_\\)">`.

## Warned, not refused

A warning is a line on the generator's report; the parser is built anyway.
Each says which engine warns.

Both engines warn of a run nested in a run, written in place or reached
through a rule or a choice:

> warning: rule `Root`: `(X+)*` repeats a run, so a run of items reads as one
> group or as several and the election picks one -- write one repetition
> unless the groups are wanted

`many(some(ref<X>))`, or `many(ref<Items>)` where `Items` can match
`some(...)`, can group the same input in several ways. Parse selection prefers
one group unless the grammar requires more. Write one repetition, or put
`append` inside it to collect one child per item.

LR warns of two runs of one item side by side:

> warning: rule `Root` reads a run of `NL` twice in a row: the run splits
> between the two in several ways -- write one repetition

`many(ref<NL>) + many(ref<NL>)`, or the same with an option between them that
may be absent: put the option inside one repetition.

Both engines warn about an unbounded repetition in a condition. It is valid,
but can slow completion on long inputs.

> warning: rule `Arrow` asks a condition of the token in hand that reads
> `('-')+` to any length …

For `!(some(lit<"-">) + lit<">">)`, use a bounded run such as
`repeat<1, 8>(lit<"-">)` when the language permits it, or a named terminal
such as `!ref<ARROW>`.

So is a branch no text reaches, by both engines: it reads a token past `eof`.

> warning: rule `Para`: branch 2 of 2 `ADORN !. EOL` is never read -- `EOL`
> stands after `eof`, which nothing can follow

`many((ref<EOL> + ref<TEXT>) | (ref<ADORN> + eof + ref<EOL>))` contains such
an unreachable branch. An option past `eof`
(`eof + option(ref<EOL>)`) is named the same way, and so is the `eof` branch
of a choice a token follows, `(eof | ref<B>) + ref<C>`. A rule past `eof` is
not: it may derive nothing. A rule left with no parse is refused instead.

A flag whose operand may read nothing is warned of by both engines.

> warning: rule `Root`: `on?=("on"?)` reads an operand that may match nothing,
> so it always sets the member -- flag the token that tells, and make the flag
> optional

`flag<&Item::on>(option(kw<"on">))` always matches, so `on` is always true.
Write `option(flag<&Item::on>(kw<"on">))`, and initialize the member to
`false`.

## Where the engines differ

Most grammar checks are shared. These cases explain the main differences
between LL and LR; generation capacity also depends on the complete grammar.

**A grammar one engine reads and the other refuses**

| Shape | LR | LL |
|---|---|---|
| One terminal's match behind two refusals of different classes | [`boundaryDisagrees`](#boundarydisagrees) | reads it |
| More than 32 implied tokens | [`gapTooMany`](#counts-a-program-cannot-hold) | has no such bound |
| An indirect left recursion where no rule has an alternative that is only a call to another rule of it | reads it | [`leftRecursion`](#leftrecursion) |
| Indirect left recursion whose competing alternatives need a different order for LL | reads it | [`leftRecursion`](#leftrecursion) |
| Large grammar | [`tooManyProductions`](#counts-a-program-cannot-hold) or [capacity refusal](#refused-for-want-of-room) | [`tooManyPositions`](#counts-a-program-cannot-hold) |

**A grammar both refuse, in different words**

| Shape | LR | LL |
|---|---|---|
| A recursive cycle that consumes no input | [`unitProductionCycle`](#unitproductioncycle) | [`leftRecursion`](#leftrecursion) |
| A left-recursive call behind an item that may match nothing, or behind more than seven optional items | [`hiddenLeftRecursion`](#hiddenleftrecursion) | [`leftRecursion`](#leftrecursion) |

**A warning only one engine gives**

LR warns of two runs of one item side by side; LL does not.
Both warn of a condition whose reach has no end, of a branch past `eof`, of a
flag whose operand may read nothing and of a run nested in a run.

## Grammar size limits {#counts-a-program-cannot-hold}

These diagnostics mean that the generated grammar exceeds a supported
capacity. The limit includes helper rules and states produced by generation,
not just the declarations you wrote.

| Code | The grammar… |
|---|---|
| `namePool` | declares too many distinct names |
| `tooManyTerminals` | declares more than 32 767 terminals |
| `tooManyRules` | declares more than 32 767 rules, auxiliaries included |
| `tooManyConfigurations` | reads under more than 255 skip configurations: the grammar's own, one for every distinct pair of lists a `skip()` names and one for every `section<>` |
| `gapTooMany` | states more than 255 gap conditions, or (LR only) more than 32 implied tokens |
| `gapLeafPool` | declares too many distinct condition expressions |
| `tokenGraphTooLarge` | has tokens too complex to generate, or uses a repetition bound above 4 294 967 295 |
| `tooManyPositions` | exceeds LL's grammar size limit |
| `tooManyProductions` | has too many alternatives after LR generation |
| `productionTooWide` | has an alternative too long for LR generation |
| `tableTooLarge` | exceeds LR's grammar size limit |

Large counted repetitions and unordered groups can reach these limits even
with few rule declarations. The largest supported repetition bound depends
on the item's body and the rest of the grammar. Use an unbounded repetition
and validate the count afterwards when a very large bound makes generation
too expensive. Reduce alternatives, factor shared expressions, or split a
large grammar into separate languages.

## Generator capacity limits {#refused-for-want-of-room}

The generator reports a capacity limit when the grammar is too large:

> refused by the 16-bit index of an LR state …

LL also reports the exceeded limit:

> refused, 70000 rules overrun sixteen bits

Reduce the grammar's size or split it into separate languages.

## Other generated grammar limits {#refused-by-the-records-stamp}

> refused by the record's stamp …

This message also means the generated grammar exceeds a supported capacity.
Both engines can report it. Reduce repetitions or alternatives as described
under [Grammar size limits](#counts-a-program-cannot-hold).
