# Derived grammars

A derived grammar builds a dialect of an existing language. It inherits the
base grammar's rules and replaces only the ones you choose.

## Extend a grammar

Here `json::JSON` is the base grammar, with a whitespace terminal `WS`.
Include its header, then declare the derived grammar:

```cpp title="src/json5/core/Json5Grammar.hpp"
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

## Add it to the build

Declare the base language first, then generate the dialect with `BASE` set
to the base's language id:

```cmake title="src/CMakeLists.txt"
cursiv_generate_language(ID         json5
    GRAMMAR    json5/core/Json5Grammar.hpp
    NAMESPACE  json5::JSON5
    EXTENSIONS .json5
    BASE       json
    OUT        json5/generated)
```

Here `json` is the `ID` of the base's `cursiv_generate_language` call. To
also serve base documents, give the base its own `EXTENSIONS` and register
its services.

## Advanced: extend a rule's body

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

## Rule constraints

An override keeps the rule's kind, and whether it builds an AST node or
returns a data value. A node rule can return the same class or a derived one,
not an unrelated class. For unrelated AST shapes, use separate grammars that
share AST types.
