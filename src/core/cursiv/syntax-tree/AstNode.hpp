#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/MultiRefItems.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/typelist.hpp"

// The typed tree the builder materialises from a CST. Every AST node derives
// from AstNode by design: the base carries the parent link, the ID of the CST
// node it was built from, and the field vocabulary.

namespace cursiv {

// Forward declarations: reference and multi_reference are defined after AstNode
// (they use its vocabulary), which needs to alias them in that same vocabulary.
template <class... Ts>
struct reference;
template <class... Ts>
struct multi_reference;

// "This site narrowed nothing": what it accepts is what the member accepts. A
// site may say less than the member's candidate pack, and the writer then
// stamps which of the field's narrowings the reference carries.
inline constexpr std::uint8_t kNoNarrow = 0xFF;

struct AstNode;

namespace detail {
struct RefAccess;
}

inline constexpr std::ptrdiff_t kNoOffset =
    std::numeric_limits<std::ptrdiff_t>::max();

// The runtime identity of one AST class, and the identity is the address:
// `&ast_type_tag<T>` is one inline variable per T, unique in the process. Never
// copy a tag or compare tags by value -- the name is decoration, and two
// classes may print the same last component.
//
// Whether a node is a T is answered here, not by the grammar: a tag carries
// the transitive closure of its own bases, which `cursiv::Extends` states and
// a consteval walk closes. A grammar's type list answers the downward
// question -- every subtype of T -- and keeps it.
struct AstTypeTag {
  // The hot half first: a subtype test reads these two words and nothing
  // else, so the name and the reader sit after them.
  //
  // Every class this one derives from, transitively, itself excluded so the
  // array names only tags complete before this one. Not inline and not a
  // bitmask, for the same reason: nothing bounds a hierarchy. A fixed
  // capacity would refuse a grammar (xsmp already declares eight), and a bit
  // needs an index -- numbering classes is not evaluable in constant
  // evaluation, a static counter is fragile across translation units, and a
  // hash collision would mean a `static_cast` that should not have happened.
  // An address needs no numbering.
  std::uint32_t baseCount = 0;
  const AstTypeTag* const* bases = nullptr;
  // For a class reaching `AstNode` through a virtual base, where a
  // `static_cast` cannot come back down: where each of `bases` sits from a
  // node's `AstNode`, then the class's own start. An offset to a virtual base
  // is known only from an object, so the first node made states them. Null for
  // any other class.
  std::ptrdiff_t* offsets = nullptr;

  // `this` first: a tag is one of its own family, and the exact test is what
  // most callers ask.
  [[nodiscard]] constexpr bool derivesFrom(
      const AstTypeTag* other) const noexcept {
    if (other == this) return true;
    for (std::uint32_t at = 0; at < baseCount; ++at)
      if (bases[at] == other) return true;
    return false;
  }

  std::string_view name;
  // The declared name of a node, read without the grammar: whether a class
  // has a text member called `name` is a fact about the class, not about who
  // parses it. Null for a class that declares none.
  //
  // A reader and not an offset: `offsetof` on a class with a base is only
  // conditionally supported, while a static cast proved by this very tag is
  // not.
  std::string_view (*readName)(const AstNode&) noexcept = nullptr;
};

namespace detail {
// A node from its `AstNode`, once the caller has proved its class: `ast_cast`
// asks, and this one is for the paths that build and read a tree, where asking
// at every member shows. A `static_cast` will not do: it cannot leave a virtual
// base.
template <class T>
[[nodiscard]] constexpr T* ast_cast_unchecked(
    std::conditional_t<std::is_const_v<T>, const AstNode*, AstNode*>
        node) noexcept;

// A member called `name`, and plain text: a node, a reference or a list that
// happens to share the word is not a name.
template <class T>
concept has_name_member = requires(const T& t) {
  requires std::same_as<decltype((t.name)), const std::string_view&> ||
               std::same_as<decltype((t.name)), const std::string&>;
};

template <class T>
consteval std::string_view (*name_reader() noexcept)(const AstNode&) noexcept {
  if constexpr (has_name_member<T>)
    return +[](const AstNode& n) noexcept -> std::string_view {
      return ast_cast_unchecked<const T>(std::addressof(n))->name;
    };
  else
    return nullptr;
}

// Whether a `static_cast` comes back down from an `AstNode` to a `T`: not
// through a virtual base.
template <class T>
concept plain_node = requires(AstNode* node) { static_cast<T*>(node); };

template <class T>
consteval std::ptrdiff_t* offsets_of() noexcept;

// Declared here, defined below: the tag's initialiser names it, and its own
// definition names tags. A class template breaks the knot -- both are only
// instantiated at a point where each is complete.
template <class T>
struct bases_of;
}  // namespace detail

// The name comes from a __PRETTY_FUNCTION__ split, so no reflection is needed.
template <class T>
inline constexpr AstTypeTag ast_type_tag{
    static_cast<std::uint32_t>(detail::bases_of<T>::kCount),
    detail::bases_of<T>::kTags.data(), detail::offsets_of<T>(),
    detail::type_name<T>(), detail::name_reader<T>()};

namespace detail {

// `Extends<Bs...>` states the direct bases; the closure is each of them and its
// own closure, deduplicated -- inheritance is a DAG, so one base may be reached
// by many paths. Each class asks only its direct bases, so paths are never
// walked.
template <class T>
struct base_closure {
  using type = TypeList<>;
};
template <class T>
  requires requires { typename T::ast_bases; }
struct base_closure<T> {
  static consteval auto closure() {
    return []<class... Bs>(TypeList<Bs...>) consteval {
      return dedupe_t<tl_cat_t<
          tl_cat_t<TypeList<Bs>, typename base_closure<Bs>::type>...>>{};
    }(typename T::ast_bases{});
  }
  using type = decltype(closure());
};

template <class T>
struct bases_of {
  using types = typename base_closure<T>::type;
  static constexpr std::size_t kCount = tl_size<types>::value;
  static constexpr auto kTags = []<class... Bs>(TypeList<Bs...>) consteval {
    return std::array<const AstTypeTag*, kCount == 0 ? 1 : kCount>{
        &ast_type_tag<Bs>...};
  }(types{});
  // One per base, then the class's own start.
  static inline constinit std::array<std::ptrdiff_t, kCount + 1> offsets = [] {
    std::array<std::ptrdiff_t, kCount + 1> out{};
    out.fill(kNoOffset);
    return out;
  }();
};

}  // namespace detail

struct Document;

struct AstNode {
  AstNode() = default;
  AstNode(const AstNode&) = default;
  AstNode& operator=(const AstNode&) = default;
  virtual ~AstNode() = default;

  AstNode* parent = nullptr;
  // Written at construction, so identity survives a detached tree and a node
  // built by hand.
  const AstTypeTag* type = nullptr;
  // An id and not a view: a `const Cst*` per node would be a replicated word,
  // and an id is the only form a snapshot can carry. `kNoNode` for a node built
  // by hand. Declared last so its 4 trailing bytes are tail padding a derived
  // class's first <=4-byte member may occupy. Final once the tree is built:
  // the node list is sorted by it and searched by it.
  NodeId cstNodeId = kNoNode;

  // The document this node belongs to, null when it is detached or built by
  // hand. The climb is the lookup: a document is an `AstNode` and the root's
  // parent, so no back-pointer is replicated and no table is kept in step.
  // Defined in `workspace/Document.cpp`, where `Document` is complete.
  [[nodiscard]] const Document* getDocument() const noexcept;

  // Plain C++: a member owns what it holds. `string_view` is the first choice
  // for text, and costs nothing; `string` holds what a converter built.
  template <class T>
  using vector = std::vector<T>;
  using string = std::string;
  using string_view = std::string_view;
  template <class T>
  using optional = std::optional<T>;
  template <class... Ts>
  using variant = std::variant<Ts...>;
  using boolean = bool;
  using int8 = std::int8_t;
  using uint8 = std::uint8_t;
  using int16 = std::int16_t;
  using uint16 = std::uint16_t;
  using int32 = std::int32_t;
  using uint32 = std::uint32_t;
  using int64 = std::int64_t;
  using uint64 = std::uint64_t;
  using float32 = float;
  using float64 = double;
  // references are vocabulary too: derived nodes use bare names
  template <class... Ts>
  using reference = cursiv::reference<Ts...>;
  template <class... Ts>
  using multi_reference = cursiv::multi_reference<Ts...>;
};

// The base is paid by every node, so its width is pinned rather than observed.
static_assert(sizeof(void*) != 8 || sizeof(AstNode) == 32);

// A node declares its base, because C++ cannot be asked for one: the deduced
// type of an alternation is the most derived base its branches share.
template <class... Bs>
struct Extends : Bs... {
  using ast_bases = TypeList<Bs...>;
};

// The same, for a base two paths will share: each path must say so, and the
// shared class is then one subobject, so `Extends<Left, Right>` over two nodes
// is one node. Only the classes declared this way pay for it.
template <class... Bs>
struct VirtualExtends : virtual Bs... {
  using ast_bases = TypeList<Bs...>;
};

namespace detail {
template <class T>
consteval std::ptrdiff_t* offsets_of() noexcept {
  if constexpr (plain_node<T>)
    return nullptr;
  else
    return bases_of<T>::offsets.data();
}

inline const char* node_address(const AstNode* node) noexcept {
  return reinterpret_cast<const char*>(node);
}

// Every base of `T`, from a real one, in the order of its tags.
template <class T>
void state_offsets(const T& node, std::ptrdiff_t* out) noexcept {
  [&]<class... Bs>(TypeList<Bs...>) {
    std::size_t at = 0;
    ((std::atomic_ref<std::ptrdiff_t>(out[at++]).store(
         reinterpret_cast<const char*>(static_cast<const Bs*>(&node)) -
             node_address(&node),
         std::memory_order_relaxed)),
     ...);
  }(typename bases_of<T>::types{});
}

// A new node of class `T` says what it is. Written here rather than by a
// constructor, which could not name the class being built.
template <class T>
AstNode* stamp_node(T& node) noexcept {
  AstNode* const base = &node;
  base->type = &ast_type_tag<T>;
  if constexpr (!plain_node<T>) {
    auto* const offsets = bases_of<T>::offsets.data();
    std::atomic_ref<std::ptrdiff_t> own(offsets[bases_of<T>::kCount]);
    // Released last, acquired here: a thread that sees the class's own offset
    // sees its bases' too, whichever thread stated them.
    if (own.load(std::memory_order_acquire) == kNoOffset) {
      state_offsets(node, offsets);
      own.store(reinterpret_cast<const char*>(&node) - node_address(base),
                std::memory_order_release);
    }
  }
  return base;
}

template <class T>
constexpr T* ast_cast_unchecked(
    std::conditional_t<std::is_const_v<T>, const AstNode*, AstNode*>
        node) noexcept {
  if constexpr (plain_node<std::remove_cv_t<T>>) {
    assert(node == nullptr || node->type == nullptr ||
           node->type->derivesFrom(&ast_type_tag<std::remove_cv_t<T>>));
    return static_cast<T*>(node);
  } else {
    // The tag says where, in what it costs to ask whether.
    using Bare = std::remove_cv_t<T>;
    if (node == nullptr) return nullptr;
    const AstTypeTag* const tag = node->type;
    // Not stamped: no tag says where, and C++ still can.
    if (tag == nullptr) return dynamic_cast<T*>(node);
    std::uint32_t at = tag->baseCount;
    if (tag != &ast_type_tag<Bare>) {
      at = 0;
      while (at != tag->baseCount && tag->bases[at] != &ast_type_tag<Bare>)
        ++at;
      if (at == tag->baseCount) return nullptr;
    }
    const std::ptrdiff_t offset =
        std::atomic_ref<std::ptrdiff_t>(tag->offsets[at])
            .load(std::memory_order_relaxed);
    // A tag written by hand, on a class no stamped node has stated yet.
    if (offset == kNoOffset) [[unlikely]]
      return dynamic_cast<T*>(node);
    using Byte = std::conditional_t<std::is_const_v<T>, const char, char>;
    return reinterpret_cast<T*>(reinterpret_cast<Byte*>(node) + offset);
  }
}
}  // namespace detail

// Errors contiguous and last: has_error is one comparison. The textual detail
// lives in the central diagnostics, keyed by handle.
enum class RefState : std::uint8_t {
  Unresolved,
  Resolving,
  Resolved,
  ErrorNotFound,
  ErrorCycle
};

constexpr bool has_error(RefState s) { return s >= RefState::ErrorNotFound; }

// A scope provider may read references, so resolution re-enters this protocol.
// Each resolution in flight is filed under its thread: a read that would wait
// on its own thread's computation, or on a thread that waits back on it,
// answers `ErrorCycle`. Work a provider hands to another thread and waits for
// is not tied to its resolution, so a cycle through it blocks instead.
struct RefHome {
  // Untyped, so one non-template hook serves every reference type.
  struct Answer {
    AstNode* target = nullptr;
    RefState state = RefState::ErrorNotFound;
    std::uint8_t which = 0;
  };

  virtual ~RefHome() noexcept = default;
  // Computes; the caller stores. `Unresolved` is a failure not to keep: the
  // next read calls the home again. Any other failure is `ErrorNotFound`.
  [[nodiscard]] virtual Answer resolve(std::uint32_t handle) const = 0;
  // Overload-set form: the targets land in `out`.
  virtual RefState resolveAll(std::uint32_t handle,
                              MultiRefItems& out) const = 0;
};

namespace detail {

// Private control value: an observed computation must publish under the wait
// coordinator's lock. No owner or mutex is stored in the reference itself.
inline constexpr RefState kRefObserved = static_cast<RefState>(0x81);
constexpr bool ref_running(RefState state) noexcept {
  return state == RefState::Resolving || state == kRefObserved;
}
constexpr RefState public_ref_state(RefState state) noexcept {
  return static_cast<RefState>(static_cast<std::uint8_t>(state) & 0x7f);
}

// Both slow paths are compiled once. The common read remains a load and a
// branch in the caller; synchronization headers stay out of generated code.
RefState resolve_one_slow(RefState& state, const RefHome* home,
                          std::uint32_t handle, AstNode*& target,
                          std::uint8_t& which);
RefState resolve_all_slow(RefState& state, const RefHome* home,
                          std::uint32_t handle, MultiRefItems& out);

// Which candidate T is, as the linker counts them: the first occurrence in
// the pack. `RefOps::branch` answers the first branch that accepts a tag, so
// a reader asking `get<T>()` must compare `which` against the same rank --
// with `reference<T, T>` the last one is an index the linker never writes.
template <class T, class... Ts>
inline constexpr std::uint8_t branch_index = [] {
  std::uint8_t i = 0, found = 0;
  bool seen = false;
  const auto step = [&](bool same) {
    if (same && !seen) {
      found = i;
      seen = true;
    }
    ++i;
  };
  (step(std::is_same_v<T, Ts>), ...);
  return found;
}();

// An rvalue that owns the bytes a view would alias: `r.bind(id, std::string())`
// would store a view of memory dying at the end of the full expression, the one
// dangle the type system can see. Asked as a shape and not at `std::string` by
// name, since every owning string type dangles alike. An lvalue is the caller's
// to keep alive; a view, pointer or array aliases something else.
template <class S>
concept owning_text_rvalue =
    !std::is_reference_v<S> && std::is_convertible_v<S, std::string_view> &&
    !std::is_same_v<std::remove_cv_t<S>, std::string_view> &&
    !std::is_pointer_v<std::decay_t<S>> && !std::is_array_v<S>;

// The refusal of an owning temporary, where only a view is kept: a
// static_assert, since g++ drops the reason of a `= delete("...")` across a
// module boundary.
template <class S>
void refuse_owning_bind() {
  static_assert(
      dependent_false<S>,
      "this name is a temporary that owns its text and is destroyed at the "
      "end of the expression, but what receives it keeps only a view of it. "
      "Bind text that lives as long as the tree: a slice of the parsed "
      "document, or text the document keeps (`document.keep_text(text)`)");
}

}  // namespace detail

// A cross-reference as an AST field: the name now, one target of Ts... once
// read. `which` is the index in Ts... written by the linker, and reading
// resolves, so a reader needs nothing but the field.
//
// The name is always borrowed -- a slice of the document, or text the tree
// keeps for a converter that built it -- so `refText` is a
// `string_view` and the payload's value type must be one. It is valid
// exactly as long as both the source buffer and the `ParseResult` are alive:
// an AST outliving either keeps good targets and dangling spellings.
//
// It is not NUL-terminated: the byte after the name is whatever the source
// wrote next, so print it as `%.*s` with `refText.size()`.
template <class... Ts>
struct reference {
  static_assert(sizeof...(Ts) >= 1);
  static_assert(sizeof...(Ts) <= 255,
                "a reference has at most 255 candidate types");
  AstNode::string_view refText;

 private:
  friend struct detail::RefAccess;
  // Written by a read (`resolve`), hence mutable.
  mutable AstNode* _target = nullptr;
  // Installed once by the document over the build's work list; null on a
  // hand-built or detached tree, where reading resolves nothing. The one
  // embedded context pointer, since a read must work with nothing in hand.
  const RefHome* _home = nullptr;
  // The CST node the name was read from. Declared after the pointers so it and
  // the three below share one tail word instead of opening a hole.
  NodeId _nodeId = kNoNode;
  std::uint32_t _handle = 0;  // rank in the document's work list; keys `home`
  mutable RefState _state = RefState::Unresolved;
  mutable std::uint8_t _which = 0;
  // The writing site's narrowing, as an index into this field's
  // `RefOps::narrows`. Written once by the builder before the reference is
  // reachable, never by a resolution, so it needs no atomic read.
  std::uint8_t _narrow = kNoNarrow;

 public:
  [[nodiscard]] RefState resolve() const {
    const RefState current =
        std::atomic_ref<RefState>(_state).load(std::memory_order_acquire);
    if (current == RefState::Resolved) return current;
    if (_home != nullptr &&
        (current == RefState::Unresolved || detail::ref_running(current)))
      return detail::resolve_one_slow(_state, _home, _handle, _target, _which);
    return detail::public_ref_state(current);
  }

  // The node it resolves to, whichever candidate that is; null until it does.
  [[nodiscard]] AstNode* target() const {
    return resolve() == RefState::Resolved ? _target : nullptr;
  }

  // One candidate needs no type argument.
  detail::pack_at_t<0, Ts...>* get() const
    requires(sizeof...(Ts) == 1)
  {
    return resolve() == RefState::Resolved
               ? detail::ast_cast_unchecked<detail::pack_at_t<0, Ts...>>(
                     _target)
               : nullptr;
  }

  template <class T>
    requires((std::is_same_v<T, Ts> || ...))
  T* get() const {
    if (resolve() != RefState::Resolved) return nullptr;
    constexpr std::uint8_t idx = detail::branch_index<T, Ts...>;
    return _which == idx ? detail::ast_cast_unchecked<T>(_target) : nullptr;
  }

  // Offered only for a single candidate; with a pack the caller names the
  // branch through `get<T>()`.
  detail::pack_at_t<0, Ts...>* operator->() const
    requires(sizeof...(Ts) == 1)
  {
    return get();
  }
  detail::pack_at_t<0, Ts...>& operator*() const
    requires(sizeof...(Ts) == 1)
  {
    return *get();
  }
  explicit operator bool() const { return resolve() == RefState::Resolved; }

 private:
  // The writer hands over the slice it already had and promises it outlives the
  // tree. `nar` is defaulted for a caller binding a name by hand.
  void bind(NodeId at, AstNode::string_view t, std::uint8_t nar = kNoNarrow) {
    _nodeId = at;
    refText = t;
    _narrow = nar;
    _state = RefState::Unresolved;
  }
};

// One name resolving to several targets. `items` is filled after the build, by
// a read.
template <class... Ts>
struct multi_reference {
  static_assert(sizeof...(Ts) >= 1);
  static_assert(sizeof...(Ts) <= 255,
                "a reference has at most 255 candidate types");
  AstNode::string_view refText;  // borrowed, see `reference`

 private:
  friend struct detail::RefAccess;
  mutable MultiRefItems _items;  // written by a read, see `reference`
  const RefHome* _home = nullptr;
  // The CST node of the name, and the tail word it shares -- see `reference`.
  NodeId _nodeId = kNoNode;
  std::uint32_t _handle = 0;
  mutable RefState _state = RefState::Unresolved;
  // The site's narrowing -- see `reference`. Same tail padding, same
  // rules: one family, one law.
  std::uint8_t _narrow = kNoNarrow;

 public:
  multi_reference() = default;
  multi_reference(const multi_reference&) = default;
  multi_reference(multi_reference&&) noexcept = default;
  multi_reference& operator=(const multi_reference&) = default;
  multi_reference& operator=(multi_reference&&) noexcept = default;

  // Same rule and same return contract as `reference::resolve`.
  [[nodiscard]] RefState resolve() const {
    const RefState current =
        std::atomic_ref<RefState>(_state).load(std::memory_order_acquire);
    if (current == RefState::Resolved) return current;
    if (_home == nullptr ||
        (current != RefState::Unresolved && !detail::ref_running(current)))
      return detail::public_ref_state(current);
    return detail::resolve_all_slow(_state, _home, _handle, _items);
  }

  // Read items only after acquiring a published result. A cycle or a detached
  // unresolved reference has no readable overload set.
  std::size_t size() const {
    return resolve() == RefState::Resolved ? _items.size() : 0u;
  }
  bool empty() const { return size() == 0; }

  detail::pack_at_t<0, Ts...>* get(std::size_t i) const
    requires(sizeof...(Ts) == 1)
  {
    if (resolve() != RefState::Resolved || i >= _items.size()) return nullptr;
    return detail::ast_cast_unchecked<detail::pack_at_t<0, Ts...>>(
        _items[i].target);
  }

  // The i-th target if it is a T, as `reference::get<T>` answers.
  template <class T>
    requires((std::is_same_v<T, Ts> || ...))
  T* get(std::size_t i) const {
    if (resolve() != RefState::Resolved || i >= _items.size()) return nullptr;
    constexpr std::uint8_t idx = detail::branch_index<T, Ts...>;
    return _items[i].which == idx
               ? detail::ast_cast_unchecked<T>(_items[i].target)
               : nullptr;
  }

 private:
  void bind(NodeId at, AstNode::string_view t, std::uint8_t nar = kNoNarrow) {
    _items.clear();
    _nodeId = at;
    refText = t;
    _narrow = nar;
    _state = RefState::Unresolved;
  }
};

// Pinned: the shared tail word is what makes `refNodeId`/`handle`/`_state`/
// `which`/`narrow` free, and the pin is what proves a new byte lands in it.
namespace detail {
struct ref_size_probe : AstNode {};
static_assert(sizeof(void*) != 8 || sizeof(reference<ref_size_probe>) == 48);
static_assert(sizeof(void*) != 8 ||
              sizeof(multi_reference<ref_size_probe>) == 64);

// What the builder and the linker's tables read and write of a reference: the
// protocol a resolution relies on, which is theirs alone.
struct RefAccess {
  template <class R>
  static auto& target(R& ref) noexcept {
    return ref._target;
  }
  template <class R>
  static auto& home(R& ref) noexcept {
    return ref._home;
  }
  template <class R>
  static auto& node_id(R& ref) noexcept {
    return ref._nodeId;
  }
  template <class R>
  static auto& handle(R& ref) noexcept {
    return ref._handle;
  }
  template <class R>
  static auto& state(R& ref) noexcept {
    return ref._state;
  }
  template <class R>
  static auto& which(R& ref) noexcept {
    return ref._which;
  }
  template <class R>
  static auto& narrow(R& ref) noexcept {
    return ref._narrow;
  }
  template <class R>
  static auto& items(R& ref) noexcept {
    return ref._items;
  }
  template <class R>
  static void bind(R& ref, NodeId at, AstNode::string_view text,
                   std::uint8_t narrowing = kNoNarrow) {
    ref.bind(at, text, narrowing);
  }
};
}  // namespace detail

// The linker's work list entry. Never a direct pointer to the reference object:
// a vector<reference<T>> relocates its elements as it grows, while the owner
// stays where it was made and the indices are frozen when the build ends.
struct RefHandle {
  // Null for a site pruned from the tree: the entry stays so ranks hold.
  AstNode* owner = nullptr;
  std::uint16_t field = 0;  // index into the grammar's field registry
  std::uint32_t index = 0;  // position when the member is a vector of refs
};
static_assert(sizeof(void*) != 8 || sizeof(RefHandle) == 16);

// What a linker can do to a reference it only knows by `RefHandle`. One entry
// per field, indexed by `RefHandle::field`; a non-reference field is null.
inline constexpr std::uint8_t kNoBranch = 0xFF;

// The one shape a type filter has: a tag in, the candidate index it satisfies
// out, `kNoBranch` for none.
using RefBranchFn = std::uint8_t (*)(const AstTypeTag*) noexcept;

// A single reference read whole: the CST node of its name and its target,
// null unless the reference resolved.
struct SingleRefRead {
  NodeId name = kNoNode;
  AstNode* target = nullptr;
};

struct RefOps {
  bool multi = false;
  // The index in `Ts...` a candidate satisfies, or `kNoBranch`. A subtype
  // answers the first branch that accepts it.
  RefBranchFn branch = nullptr;

  // One entry per narrow type declared on this member, in first-occurrence
  // order; the reference carries the index. An entry answers what `branch`
  // answers, since a narrow type derives from exactly one candidate, so there
  // is no second notion of "which candidate".
  const RefBranchFn* narrows = nullptr;
  std::uint8_t narrow_count = 0;
  // The narrow types themselves, same order: what a failed lookup names as
  // "expected".
  const AstTypeTag* const* narrow_tags = nullptr;
  // The index the writer stamped on one reference, read the way every other
  // column of this row is read -- through the handle, without the type.
  std::uint8_t (*narrow_of)(const AstNode& owner,
                            std::uint32_t index) noexcept = nullptr;

  // The eager linker walks the work list calling this, so forcing takes the
  // same CAS, guard and publication as an ordinary read. Not `noexcept`: the
  // provider it reaches may throw, and the guard makes that safe.
  RefState (*force)(const AstNode& owner, std::uint32_t index) = nullptr;

  // Reading. `index` selects the element when the member is a vector of
  // references; it is ignored otherwise.
  std::string_view (*text)(const AstNode& owner,
                           std::uint32_t index) noexcept = nullptr;
  NodeId (*name_id)(const AstNode& owner,
                    std::uint32_t index) noexcept = nullptr;
  RefState (*state)(const AstNode& owner,
                    std::uint32_t index) noexcept = nullptr;
  // 0 or 1 for a single reference, the item count for a multi one.
  std::size_t (*size)(const AstNode& owner,
                      std::uint32_t index) noexcept = nullptr;
  AstNode* (*at)(const AstNode& owner, std::uint32_t index,
                 std::size_t which) noexcept = nullptr;
  // What `name_id`, `size` and `at` answer together, in one read of the
  // member. Null for a multi reference.
  SingleRefRead (*single)(const AstNode& owner,
                          std::uint32_t index) noexcept = nullptr;

  void (*reset)(AstNode& owner, std::uint32_t index) noexcept = nullptr;
};

// One reference site's type filter: the field's row plus the site's declared
// narrowing.
struct RefFilter {
  const RefOps* ops = nullptr;
  std::uint8_t narrow = kNoNarrow;

  // Which candidate a target satisfies, or `kNoBranch`: one indexed load when
  // the site narrowed, the member's own row when it did not.
  // Precondition: `ops` names a reference field.
  [[nodiscard]] std::uint8_t branch(const AstTypeTag* t) const noexcept {
    return narrow < ops->narrow_count ? ops->narrows[narrow](t)
                                      : ops->branch(t);
  }
};

// This class or a subtype: the tag carries the closure of its bases, and the
// test walks it -- an `Entity` is the `Type` it extends. The downward family
// question -- every subtype of T -- needs the grammar's type list, which this
// header deliberately does not know: `AstReflection::getAllSubTypes` is that
// door.
// `T` is stripped of its cv-qualifiers before the tag is taken:
// `ast_type_tag<const X>` is a different inline variable from
// `ast_type_tag<X>`, so `ast_cast<const X>` would otherwise get a silent null
// forever.
template <class T>
[[nodiscard]] constexpr const T* ast_cast(const AstNode* node) noexcept {
  return node != nullptr && node->type != nullptr &&
                 node->type->derivesFrom(&ast_type_tag<std::remove_cv_t<T>>)
             ? detail::ast_cast_unchecked<const T>(node)
             : nullptr;
}

using AstNodePtr = std::unique_ptr<AstNode>;

namespace detail {
// A new `T`, outside any tree's arena: its members at their defaults, its
// parent null. `AstReflection::createNode` is the door.
template <class T>
[[nodiscard]] AstNodePtr new_ast_node() {
  auto node = std::make_unique<T>();
  stamp_node(*node);
  return node;
}
}  // namespace detail

}  // namespace cursiv
