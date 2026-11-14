# AST vocabulary

The abstract syntax tree (AST) holds your language's data as C++ structs.
Declare each node with `cursiv::Extends<Base>`, where `Base` is
`cursiv::AstNode` or another node class:

```cpp
#include "cursiv/syntax-tree/AstNode.hpp"

struct Person : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
```

## Member types

| Type | Stores |
|---|---|
| `string_view` | Text from the source; prefer it, it costs nothing |
| `string` | Text a converter built; it owns its bytes |
| `boolean` | A `bool` |
| `int8`…`int64`, `uint8`…`uint64` | Sized integers |
| `float32`, `float64` | A `float` or `double` |
| `optional<T>` | An optional value |
| `variant<Ts...>` | One value from several types |
| `vector<T>` | A list (`std::vector<T>`) |
| `T*` | A child AST node |
| `reference<T>` | A reference to a named node |
| `reference<A, B>` | A reference to a node of type `A` or `B` |
| `multi_reference<T>` | One name resolving to several nodes |

A member can hold any type a converter returns, such as a `struct Uuid`, a
`std::map` or a class of your own. A list or an `optional` builds its value in place,
so the type needs no default constructor or assignment; a list needs it to move. A
member held directly already has a value when the node is built, and an assignment
replaces it: give a type with no default constructor a first value, as in
`Uuid id{0};`.

Use `string_view` for text whenever you can. It holds the matched text, a slice
of it that a converter returns, or the parts of a data rule. A converter that returns
a `std::string` needs a `string` member, and a `string_view` over it is refused. A
`string_view` points into the source text or the `ParseResult`, so keep both alive
while you read the AST.
`Parser::parse` refuses a temporary `std::string` at compile time for that reason: name
the text first.

A `vector<T>` member grows with `append`. A rule that returns the whole
`std::vector<T>` fills it with `assign` instead.

One terminal can feed both kinds of member. Its converter returns a slice, and a
data rule decodes that slice:

```cpp
struct STRING : terminal<as<between_quotes>(lit<"\""> + text + lit<"\"">)> {};
struct Decoded : rule<as<decode_escapes>(ref<STRING>)> {};
```

A `string_view` member on `ref<STRING>` allocates nothing but holds escapes as
written, so use it only where the language refuses them. A member that needs the
value is a `string` on `ref<Decoded>`.

Treat a tree as read-only once it is built. Its services rely on it: adding to a
list does not set a node's parent, and changing a `string` that names a node leaves
the index pointing at the old text.

## Child nodes

A child node is a `T*`, and `vector<T>` is `std::vector<T>`. A list of nodes is
`vector<Person*> persons;`:

```cpp
for (const Person* person : model.persons) use(*person);
```

The `ParseResult` owns every node of the tree and destroys them with it. A node
it built lives exactly as long as the `ParseResult`.

## Several bases

`Extends` takes several bases. When two of them derive from a same node class,
declare that link with `VirtualExtends`, so the class is one base:

```cpp
struct Named : cursiv::VirtualExtends<cursiv::AstNode> {
  string name;
};
struct Typed : cursiv::VirtualExtends<Named> {};
struct Documented : cursiv::VirtualExtends<Named> {};
struct Field : cursiv::Extends<Typed, Documented> {};
```

`VirtualExtends` costs a little time and memory, and only the classes that use
it pay for it.

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
| `.get()` | The target, or null; only with a single candidate type |
| `.get<T>()` | The target from candidate `T`, or null |
| `operator->`, `operator*` | The target of a resolved single-candidate reference |
| `explicit operator bool` | Whether the reference resolved |
| `.target()` | The target as an `AstNode*`, whichever candidate it is, or null |
| `.refText` | The name as written |

Check `.get()` for null before you use the target. Read the targets of a
`multi_reference<T>` with `.size()` and `.get(index)`, or `.get<T>(index)`
with several candidate types.

## Identify a node's type

`ast_cast<T>` returns the node as a `T` when its class is `T` or derives
from `T`. It returns null otherwise, and for a null node:

```cpp
const auto* model = cursiv::ast_cast<recipe::ast::Model>(root);
```

For an exact class match, compare `node->type == &cursiv::ast_type_tag<T>`.

## Walk a tree

`cursiv/syntax-tree/AstWalk.hpp` walks down through the language's reflection and
up through the parents:

```cpp
const cursiv::AstReflection& reflection = *services.astReflection;

// The nodes directly inside `node`.
cursiv::visit_contents(reflection, node, [&](cursiv::AstNode& child) { /* ... */ });

// Every node below `node`, a parent first. Return AstWalk::Prune to skip what
// a node contains, or return nothing to read them all.
cursiv::visit_all_contents(reflection, node, [&](cursiv::AstNode& child) {
  return cursiv::ast_cast<ast::Comment>(&child) != nullptr
             ? cursiv::AstWalk::Prune
             : cursiv::AstWalk::Enter;
});

// The node itself or the nearest node containing it of that class, or null.
const auto* entity = cursiv::container_of_type<ast::Entity>(&node);
```

## Read a node's comment

`commentProvider->getComment(node)` gives a comment's text. To report on a tag of
the comment, `cursiv/documentation/CommentNode.hpp` gives its place in the
document:

```cpp
const std::vector<std::string> rules{"ML_COMMENT"};
const cursiv::NodeId comment = cursiv::find_comment_node(document, node, rules);
if (comment != cursiv::kNoNode) {
  const cursiv::DocComment doc =
      cursiv::parse_doc_comment(document, comment);
  if (const auto* tag = doc.getTag("uuid")) {
    // tag->range.start.offset and tag->range.end.offset are document offsets.
  }
}
```

## Advanced: node metadata

| Member | Meaning |
|---|---|
| `parent` | The containing node; an attached root's parent is its `Document` |
| `getDocument()` | The containing document, or null for a detached tree |
| `type` | The runtime type tag; `type->name` names the class |
| `cstNodeId` | The concrete syntax tree (CST) node it was built from |

A property of your grammar may have one of these names, and then hides the
member in its class: `entity.parent` is the property. `cursiv::parent_of(node)`,
`cursiv::type_of(node)` and `cursiv::cst_node_id_of(node)` always read the
node's own.

A node cannot be copied or assigned: it is one place in one tree. Make a new
one with `AstReflection::createNode` and set its properties.
