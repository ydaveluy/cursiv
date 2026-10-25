# AST vocabulary

The abstract syntax tree (AST) stores the language's data as C++ structs.
Declare each node with `cursiv::Extends`:

```cpp
#include "cursiv/core/syntax-tree/AstNode.hpp"

struct Person : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
```

`Extends<Base>` declares the base class and provides the member types below.
To derive another AST node from `Person`, use `cursiv::Extends<Person>`.

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

Use `string_view` rather than an owning string member. It refers to existing
text without owning it. Keep both the source text and `ParseResult` alive
while reading the AST.
Text allocated by converters belongs to the parse result.

## `vector<T>` is not `std::vector<T>`

Use pointers for lists of AST nodes:

```cpp
vector<Person*> persons;
```

A `vector<Person>` or `std::vector<Person*>` AST member is rejected.
List elements must be trivially copyable: plain values and node pointers meet
that requirement.

Read a list with `size()`, `empty()`, `operator[]`, `begin()`, `end()`,
`back()` or `data()`. It also converts to `std::span<const T>`. It has no
`front()` or `insert()`; the parser fills the list through grammar directives.

## References

A reference stores the written name, then resolves it to a declaration:

```cpp
struct Greeting : cursiv::Extends<cursiv::AstNode> {
  reference<Person> person;
};
```

Parsing alone leaves references unresolved. Build the document to prepare its
scopes and resolve references; see [Document lifecycle](services.md#document-lifecycle).

| Access | Result |
|---|---|
| `.get()` | The target, or null; available for a single candidate type |
| `.get<T>()` | The target from candidate `T` in the declared type list, or null |
| `operator->`, `operator*` | Access to a resolved single-candidate reference |
| `explicit operator bool` | Whether the reference resolved |
| `.refText` | The name as written |
| `.state()` | The resolution state |

Check the result of `.get()` before dereferencing it. Accessors may resolve a
reference lazily once scopes have been computed.

For a `multi_reference<T>`, use `.size()` and `.get(index)` to read all
targets. With several candidate types, use `.get<T>(index)`.

## Identify a node's type

Given an `AstNode*` named `root`, use `ast_cast` to check its class before
reading members:

```cpp
const auto* model = cursiv::ast_cast<recipe::ast::Model>(root);
```

The result is null if `root` is null or has an incompatible type. Derived
classes are accepted too.

## Advanced: node metadata

Each node provides metadata for navigation and custom services:

| Member | Meaning |
|---|---|
| `parent` | The containing node; an attached root's parent is its `Document` |
| `getDocument()` | The containing document, or null for a detached tree |
| `type` | The node's runtime type tag |
| `cstNodeId` | The concrete syntax tree (CST) node it was built from |

A type tag's `name` identifies its class. For an exact type comparison, use
`node->type == &cursiv::ast_type_tag<T>`; for a class or any of its derived
classes, use `ast_cast<T>`.
