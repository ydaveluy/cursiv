# AST vocabulary

The abstract syntax tree (AST) holds your language's data as C++ structs.
Declare each node with `cursiv::Extends<Base>`, where `Base` is
`cursiv::AstNode` or another node class:

```cpp
#include "cursiv/core/syntax-tree/AstNode.hpp"

struct Person : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
```

## Member types

| Type | Stores |
|---|---|
| `string_view` | Text from the source or a converted value |
| `boolean` | A `bool` |
| `int8`…`int64`, `uint8`…`uint64` | Sized integers |
| `float32`, `float64` | A `float` or `double` |
| `optional<T>` | An optional value |
| `variant<Ts...>` | One value from several types |
| `vector<T>` | A list |
| `T*` | A contained AST node |
| `reference<T>` | A reference to a named node |
| `reference<A, B>` | A reference accepting either candidate type |
| `multi_reference<T>` | One name resolving to several nodes |

Text members are `string_view`, never owning strings: they point into the
source text, or into the `ParseResult` for converted values. Keep both alive
while reading the AST.

## `vector<T>` is not `std::vector<T>`

A list of nodes holds pointers: `vector<Person*> persons;`. A `vector<Person>`
or `std::vector<Person*>` member is rejected, and elements must be trivially
copyable. The parser fills the list; read it with `size()`, `empty()`,
`operator[]`, `begin()`, `end()`, `back()`, `data()`, or as a
`std::span<const T>`.

## References

A reference stores the name as written:

```cpp
struct Greeting : cursiv::Extends<cursiv::AstNode> {
  reference<Person> person;
};
```

Parsing alone does not resolve it: build the document first, as described in
[Document lifecycle](services.md#document-lifecycle).

| Access | Result |
|---|---|
| `.get()` | The target, or null; for a single candidate type |
| `.get<T>()` | The target from candidate `T`, or null |
| `operator->`, `operator*` | The target of a resolved single-candidate reference |
| `explicit operator bool` | Whether the reference resolved |
| `.refText` | The name as written |
| `.state()` | The resolution state |

Check `.get()` for null before using the target. A `multi_reference<T>` reads
its targets with `.size()` and `.get(index)`, or `.get<T>(index)` with several
candidate types.

## Identify a node's type

`ast_cast<T>` returns the node as a `T` when its class is `T` or derives from
it, and null otherwise or for a null node:

```cpp
const auto* model = cursiv::ast_cast<recipe::ast::Model>(root);
```

For an exact class match, compare `node->type == &cursiv::ast_type_tag<T>`.

## Advanced: node metadata

| Member | Meaning |
|---|---|
| `parent` | The containing node; an attached root's parent is its `Document` |
| `getDocument()` | The containing document, or null for a detached tree |
| `type` | The runtime type tag; `type->name` names the class |
| `cstNodeId` | The concrete syntax tree (CST) node it was built from |
