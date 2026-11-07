# Reuse a grammar

You can reuse an existing grammar in two ways: derive a grammar that
replaces some of its rules or parses only a fragment, or embed it inside the
documents of another language.

## Derive a dialect

A derived grammar inherits the base grammar's rules and replaces only the
ones you choose. Here `json::JSON` is the base grammar, with a whitespace
terminal `WS`. Include its header, then declare the derived grammar:

```cpp title="src/core/json5/Json5Grammar.hpp"
namespace json5 {

struct JSON5 : cursiv::extends<json::JSON> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, JSON::WS>
  struct override<T> : terminal<some(cr<R"( \t\n\r\v\f)"> | lit<"\xc2\xa0">
                                     | lit<"\xef\xbb\xbf">)> {};
};

}  // namespace json5
```

The `requires` clause names the base rule to replace. This `WS` also accepts
non-breaking spaces and a byte-order mark, and every use of `JSON::WS` in the
derived grammar gets it.

### Add it to the build

Generate the base language first, then the dialect, with `BASE` set to the
`ID` of the base's `cursiv_generate_language` call:

```cmake title="src/CMakeLists.txt"
cursiv_generate_language(ID         json5
    GRAMMAR    json5/Json5Grammar.hpp
    NAMESPACE  json5::JSON5
    EXTENSIONS .json5
    BASE       json
    OUT        json5/generated)
```

To also serve base documents, give the base its own `EXTENSIONS` and
register its services.

### Extend a rule's body

`super` stands for the replaced rule's body. In place of the override above,
this one adds non-breaking spaces to the base's whitespace:

```cpp
  template <class T>
    requires std::same_as<T, JSON::WS>
  struct override<T>
      : terminal<lit<"\xc2\xa0"> | super> {};
```

An override keeps the base rule's converter unless it supplies one with
`terminal<as<X>(Body)>`. See [Conversion](../reference/grammar.md#conversion).

### Share rules without a base language

A base does not have to be a language. Leave out `entry`, and its rules are
only there to be shared. Each derived grammar then names its own `entry`. Do not
generate the base, and leave out `BASE` when you generate a derived grammar:

```cpp
struct Common : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-z"> + many(w)> {};

  using ignored = WS;
};

struct Config : cursiv::extends<Common> {
  struct Root : rule<create<ast::Config>(
                    kw<"config"> + assign<&ast::Config::name>(ref<ID>))> {};

  using entry = Root;
};
```

### Parse a fragment

A parser reads its grammar's `entry`, which is fixed when the parser is
generated. To parse a smaller piece, such as one expression typed in a REPL,
derive a grammar that names that rule as its entry:

```cpp title="src/core/arithmetics/ExpressionGrammar.hpp"
namespace arith {

struct ArithExpression : cursiv::extends<Arith> {
  using entry = Arith::Expression;
};

}  // namespace arith
```

Generate it as a language of its own, without `EXTENSIONS` so that it claims
no files, and link `arithexprGenerated` where you call it:

```cmake title="src/CMakeLists.txt"
cursiv_generate_language(ID         arithexpr
    GRAMMAR    arithmetics/ExpressionGrammar.hpp
    NAMESPACE  arith::ArithExpression
    BASE       arithmetics
    OUT        arithmetics/generated)
```

```cpp
#include "arithmetics/generated/ArithExpression/Module.hpp"

const auto parser = arith::generated::ArithExpression::create_parser();
const cursiv::ParseResult result = parser->parse("1 + 2 * 3");
const auto* expression = cursiv::ast_cast<arith::ast::Expression>(result.value);
```

The fragment's parser holds only the rules its entry reaches. Generate one
grammar for each entry you need.

### Rule constraints

An override keeps the rule's kind, and whether it builds an AST node or
returns a data value. A node rule can return the same class or a derived one,
not an unrelated class. For unrelated AST shapes, use separate grammars that
share AST types.

## Embed a grammar

`section<G>(open, close)` reads grammar `G` between two delimiters. Here a
document holds JSON blocks between `'''` marks:

```cpp
struct Doc : cursiv::GrammarSpec {
  struct Block
      : rule<create<ast::Block>(
            kw<"json"> + assign<&ast::Block::json>(
                             section<json::JSON>(lit<"'''">, lit<"'''">)))> {};
  // ...
};
```

The enclosing rule reads the delimiters, and `json::JSON` reads the content
with its own trivia and keywords. Only `Doc` is generated: include the JSON
grammar's header in its header. The AST mixes nodes from both grammars. This
combines parsers only: the enclosing language must provide any validation or
scoping for the JSON nodes. See
[Embedded grammars](../reference/grammar.md#embedded-grammars) for the
delimiter restrictions.
