# Derived grammars

Use a derived grammar to build a dialect of an existing language. It inherits
the base grammar's rules and can replace selected rules without copying them.

## Extend a grammar

Assume `json::JSON` is your base grammar, with a whitespace terminal named
`WS`. Include its header, then declare the derived grammar:

`src/json5/core/Json5Grammar.hpp`:

```cpp
namespace json5 {

struct JSON5 : cursiv::extends<json::JSON> {
  template <class>
  struct override;

  template <class T>
    requires std::same_as<T, JSON::WS>
  struct override<T> : terminal<some(cr<R"( \t\n\r\v\f)"> | lit<"\xc2\xa0"> |
                                     lit<"\xef\xbb\xbf">)> {};
};

}  // namespace json5
```

This override accepts whitespace including non-breaking spaces and a
byte-order mark. The `requires` clause selects the base rule being replaced.
References to `JSON::WS` use this definition throughout the derived grammar.

## Add it to the build

Declare the base language first, then generate the dialect with `BASE`
pointing to its CMake language id:

`src/CMakeLists.txt`:

```cmake
cursiv_generate_language(ID         json5
    GRAMMAR    json5/core/Json5Grammar.hpp
    NAMESPACE  json5::JSON5
    EXTENSIONS .json5
    BASE       json
    OUT        json5/generated)
```

Here `json` is the id from the base's `cursiv_generate_language` call.
A base with no `EXTENSIONS` is only used through its dialects. Give the base
its own extensions and register its services if you also want to serve base
documents.

## Advanced: extend a rule's body

`super` inserts the replaced rule's body. For example, use this `WS`
override in place of the one above to add non-breaking spaces while keeping
the base's whitespace definition:

```cpp
  template <class T>
    requires std::same_as<T, JSON::WS>
  struct override<T>
      : terminal<lit<"\xc2\xa0"> | super> {};
```

An override can supply a converter with `terminal<Body, Converter>`; otherwise
it inherits the base rule's converter. See
[Conversion](../reference/grammar.md#conversion).

## Rule constraints

An override preserves the rule's kind and whether it builds an AST node or
returns a data value. A node-producing rule can return the original class or
a derived class. It cannot return an unrelated class or become a data rule.
For unrelated AST shapes, use separate grammars and share common AST types.
