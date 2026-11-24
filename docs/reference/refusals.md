# Grammar errors and fixes

The generator prints the language ID, `refused:` and a message when it cannot
generate the parser. Search this page for a few words from that message.
Errors about C++ types or grammar declarations may instead come from the
compiler while it builds the generator or the generated module.

## Rules and repetitions

| Message excerpt | What to change |
|---|---|
| `the entry is a terminal` | Declare an entry rule that reads the terminal. |
| `a cycle of rules reads no input` | Make the cycle consume a token. For an optional list use `many(ref<Item>)` with a nonempty item. |
| `an unordered group has too many items` | Use at most eleven items. For larger sets, parse a list and check duplicates in validation. |
| `a counted repetition in an unordered group` | Use `option(x)` or `many(x)` for an optional item. Put a bounded run in a separate rule if the group needs one. |
| `a rule repeats what it reads too many times to be written out` | Reduce the count or move the repeated phrase to a rule. A single expanded alternative can hold up to 4,096 items. |
| `a member that holds one value is written once into a node` | The message names the rule and property. Use `append` for multiple values, or put the writes on separate alternatives or separate nodes. |
| `unsupported grammar expression` | Move the named expression to a context that accepts it. In particular, use captures inside a terminal or a delimited token. |

A warning that a flag `sets` a property `even when its flag operand reads no token`
means the property can be true on empty input. To record the presence of a
keyword, use `option(flag<&Decl::isStatic>(kw<"static">))`.

## Tokens and layout

| Message excerpt | What to change |
|---|---|
| `reads no text` | Make the token consume input. For example, declare digits with `some(d)` and make the call optional in the rule. |
| `a character class matches no character` | Give the class at least one character. |
| `a trivia rule is not a terminal` | Use terminals for whitespace and comments. |
| `which its trivia scope passes` | The rule asks for a token it skips. Remove that call or change its trivia scope. |
| `is read both inside a trivia scope and outside it` | Make the alternatives agree on which trivia to keep or skip before their shared token. |
| `is a second indent or dedent terminal` | Declare each indentation terminal only once. |
| `an indentation bracket is not one token` | Name each opening or closing bracket by its token, using a choice for several spellings. |
| `an implied token cannot be a bracket named by indent_open` | Spell the opening and closing indentation brackets in the input. |
| `a gap test's pattern is not regular` | Simplify the pattern that tests the text between tokens. |
| `that they cannot test` | In `gap::has` and `gap::is`, use literals, classes, `any`, sequences, alternatives or repetitions. A gap pattern cannot contain `through` or `eof`. |
| `with its tag is not set on every path` | Put a matching `mark<Tag>` before `same<Tag>` in the same terminal on every path. |
| `a token is settled too far past its end` | Shorten the assertion that looks beyond the end of the token. |

A message containing `cannot be read (` includes the terminal name and the reason
in parentheses. The following excerpts identify the part to simplify.

| Reason | What to change |
|---|---|
| `a call to a rule`, `a construct only a rule reads` | Move that expression into a parser rule. A terminal matches a single token. |
| `more than four tags of marks in one terminal`, `a mark in a repetition counted past what the lexer unrolls` | Use fewer capture tags or move the capture outside the counted repetition. |
| `conditions nested too deep`, `a condition that reads too far past the token` | Flatten nested lookaheads or shorten the tested text. |
| `a keyword's test inside a condition`, `a condition inside a condition that reads too far`, `a condition with unbounded lookahead` | Shorten the lookahead or test it once in the enclosing rule. |
| `too many conditions left to read`, `an end reached while a condition waits` | Reduce overlapping lookaheads or move the condition to a rule. |
| `a closer with a guard or in either case`, `a condition inside a closer's condition`, `a closer that is no literal`, `a run with no closer` | Use a distinct literal delimiter or simplify the closing condition. |
| `a closer too long to test`, `closers one inside the other` | Use a shorter, distinct delimiter. |

## Sections and conditions

| Message excerpt | What to change |
|---|---|
| `a section closes on more than one token` | Use a single closing token in `section<G>(open, close)`. |
| `the grammar reaches the same token` | Make alternatives distinguish the section before its contents begin. |
| `on a token that the embedded grammar` | Pick a delimiter that cannot match or start an embedded token. |
| `the grammar uses indentation and embeds a grammar`, `cannot use indentation` | Use indentation-free grammars on both sides of a section. |
| `a section is parsed by rules` | Give the embedded grammar an entry rule. |
| `is allowed in a rule, not in a condition`, `is allowed in a rule, not in a terminal` | Read the section in a rule. Test its opening token when you need a condition. |
| `a condition cannot test a data rule that calls itself` | Test the first tokens of the recursive rule instead. |
| `this condition of an implied token`, `asks two things of one text` | Use `eof`, a gap test or a lookahead. A conjunction can combine one gap test with one token test. Use a choice for alternatives. |

A **warning** containing `but the generator could not check that` means it
could not establish whether an embedded token overlaps the closing delimiter.
Generation continues. Test the delimiter beside each spelling the embedded
token accepts.

## C++ grammar declarations

The compiler usually identifies the rule, member or converter in its template
arguments. These are excerpts from the checks in the grammar DSL.

| Message excerpt | What to change |
|---|---|
| `this grammar names no entry rule`, `a rule is used through`, `names a grammar: read it with` | Declare `using entry = Root;` and call rules with `ref<Rule>`. Embed a grammar with `section<G>(open, close)`. |
| `this grammar declares` | Use `ignored` and `hidden` for grammar settings, and `ignore<…>` and `hide<…>` inside `skip`. |
| `a grammar replaces the same target twice` | Put each target in the local `overrides` list once. |
| `two parent grammars` | Resolve conflicting trivia lists, case settings, word boundaries, overrides or converters in the derived grammar. |
| `an override must keep the kind`, `this override changes whether the rule builds a node` | Keep the rule kind and result family. |
| `stands for the body of the overridden rule` | Use `super` inside an override with an unambiguous parent body. |
| `this override builds a class outside`, `this override gives a value of another type` | Return a compatible node or keep the data rule’s original value type. |
| `must be a character class`, `must be one character class` | Set `word_boundary` to one class or a union of classes. |
| `lit<> needs a non-empty literal`, `the guard G must be a grammar expression` | Supply text to `lit` and a grammar expression for its optional guard. |
| `Min must not exceed Max`, `Max must be at least one`, `an unordered group needs at least two items` | Check the bounds of `repeat` and the number of unordered items. |
| `must be the direct item of option`, `a commit is allowed in a rule` | Put `commit(prefix, body)` directly inside a quantifier that can stop early. |
| `levels(...) takes only precedence levels`, `an infix rule names zero or three members` | Supply precedence levels in binding order. Give all three binary member pointers when overriding the defaults. Unary member pointers must belong to the level's class or one of its bases. |
| `this character class`, `two complemented classes`, `this complement matches no character`, `is one raw byte, not a class` | Check the escapes, range endpoints, Unicode scalars and complement. Use `byte` on its own for raw input. See [character classes](grammar.md#character-class-details). |
| `this is not a grammar expression`, `joined to something that is not one`, `Seq needs at least two elements`, `Choice needs at least two alternatives` | Write literals with `lit<…>` and calls with `ref<…>`. Build sequences with `+` and choices with `\|`. |

## Nodes, properties and conversions

| Message excerpt | What to change |
|---|---|
| `this alternative can create two nodes`, `a sequence builds one node`, `starts a second node` | Build one current node and store its children with `assign` or `append`. |
| `item as a rule and use ref<> to it` | Declare the item as a rule and call it in the list. |
| `this alternative returns a node that the text may leave out`, `with nothing before it` | Create or adopt a node on every path before returning or wrapping it. |
| `current node was written, created or nested` | Adopt the called node before writing to it, or store it as a child. |
| `this directive writes a member`, `this nest stores the current node` | Put the property on a class available on every path. Give a nest a property that accepts every possible current node. |
| `unrelated classes: add create<T>`, `cannot find the class this alternative builds`, `no common base` | Use an explicit `create<T>` or give the alternatives a common node base. |
| `an AST node type must derive from cursiv::AstNode`, `kAstTypeTag<T> needs the definition of T` | Include the complete node class and derive it with `cursiv::Extends<Base>`. |
| `the member must belong to T`, `the operator and operand members must belong` | Choose properties declared by the wrapper or operator class, or by its bases. |
| `so this directive never runs` | Move the write outside the condition, `through` or capture. |
| `the member must be bool`, `an AST list member holds nodes by pointer` | Use `bool` for a flag and `vector<T*>` for a list of child nodes. |
| `no alternative of the member takes the value written`, `a value member has a default conversion or is built from a string` | Check the member type and converter result, including the alternatives of a variant. |
| `a reference has at most 255 candidate types` | Reduce the number of candidate types in the reference declaration. |
| `a rule takes at most one converter`, `combine the two conversions` | Apply one converter per value. For a chain of conversions, write a single function. |
| `f must take the text`, `f accepts more than one kind of input` | Give the converter one signature taking the text, the text parts or no argument. |
| `converts the whole token` | Use `terminal<as<X>(body)>` for one lexical conversion or `rule<as<A>(a) | as<B>(b)>` for per-branch values. |
| `the alternatives of this data rule`, `as<X>(e) converts text` | Convert text before building a node or writing a property. Make every alternative return the declared common value type. |
| `no default converter for this member type`, `no default_converter<T> for this type` | Provide a converter for the type or use `as<f>(e)`. |
| `a temporary that owns its text` | Keep the input alive while reading the tree. Use owned storage for text that a converter builds. |

See [building the tree](grammar.md#building-the-tree) and [AST types](ast.md)
for examples.

## Grammar size limits

| Message excerpt | What to change |
|---|---|
| `more than LR_MAX_STATES allows` | Raise the [state limit](cmake.md#cursiv_generate_language) to at least the minimum in the message, or set it to zero for no cap. |
| `the grammar has too many terminals`, `the grammar has too many sets of terminals`, `the grammar has too many lexer modes` | Reuse terminal definitions and simplify alternatives that expect different sets of tokens. |
| `the grammar has too many trivia scopes` | Reduce the number of distinct trivia configurations. The limit is 64. |
| `the terminals are too large for one lexer` | Simplify overlapping terminal patterns or move choices between tokens into rules. |
| `the terminals start in too many ways for one lexer` | Reuse terminal definitions and reduce the literals and keywords that begin with different text. |
| `the grammar has too many ties of terminals` | Reduce the terminals that can read the same text in one place. |
| `the grammar's states act in too many ways`, `the grammar takes too many kinds of steps`, `a rule is called from too many kinds of places`, `a state has too many actions on one token` | The parser tables are full. Lower the [state limit](cmake.md#cursiv_generate_language), or merge rules that differ only in the node they build. |

For a limit on rules, properties or actions, factor repeated directives into
named rules. For a limit on reference candidates, narrow the target types of
the affected reference.
