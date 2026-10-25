#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/MultiRefItems.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/core/utils/typelist.hpp"

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

// The list member, seated in the tree's arena: a `std::vector` member would be
// autonomous, and nearly every global allocation an AST build makes is one of
// those growing. The arena is a parameter, not a member, so no node carries an
// allocator. A block still at the top of the bump extends in place, which is
// the normal case since two members never interleave their appends.
//
// Trivially copyable elements only, and no destructor: relocation is a memcpy
// and teardown is the arena releasing its blocks. The storage is the tree's, so
// a `cursiv::vector` lives exactly as long as the `ParseResult`.
template <class T>
class vector {
  static_assert(std::is_trivially_copyable_v<T>,
                "an AST list member holds trivially copyable elements: the "
                "arena relocates by memcpy and runs no destructor. A list of "
                "values that own something is a list of POINTERS to nodes that "
                "do");
  static_assert(!std::is_base_of_v<AstNode, T>,
                "an AST list member holds nodes by pointer: `vector<Person*>`, "
                "not `vector<Person>` -- a node is built in the tree, wired to "
                "its parent, and never copied into a list");

 public:
  using value_type = T;
  using size_type = std::uint32_t;

  vector() = default;

  // The arena is a template parameter so this header never names `AstArena`,
  // which includes this one and not the other way round. The arena owes
  // `carve_bytes(size, align)` and `try_extend(p, oldBytes, newBytes)`.
  template <class Arena>
  void push_back(Arena& a, const T& v) {
    if (_size == _cap) [[unlikely]]
      grow(a);
    _data[_size++] = v;
  }

  const T* data() const noexcept { return _data; }
  T* data() noexcept { return _data; }
  std::size_t size() const noexcept { return _size; }
  bool empty() const noexcept { return _size == 0; }
  const T* begin() const noexcept { return _data; }
  const T* end() const noexcept { return _data + _size; }
  T* begin() noexcept { return _data; }
  T* end() noexcept { return _data + _size; }
  const T& operator[](std::size_t i) const noexcept { return _data[i]; }
  T& operator[](std::size_t i) noexcept { return _data[i]; }
  const T& back() const noexcept { return _data[_size - 1]; }
  T& back() noexcept { return _data[_size - 1]; }
  operator std::span<const T>() const noexcept { return {_data, _size}; }

 private:
  // Out of line: inside `push_back` the throw path charged the hot one.
  [[noreturn, gnu::noinline]] static void overflowed() {
    throw std::length_error("cursiv::vector: capacity exceeds addressable storage");
  }

  template <class Arena>
  [[gnu::noinline]] void grow(Arena& a) {
    // The element count and byte count must both fit, including on wasm32.
    constexpr std::size_t byBytes =
        std::numeric_limits<std::size_t>::max() / sizeof(T);
    constexpr std::size_t max = byBytes < 0xFFFFFFFFu ? byBytes : 0xFFFFFFFFu;
    if (_cap > max / 2 || kFirst > max) overflowed();
    const std::size_t want = _cap == 0 ? kFirst : std::size_t{_cap} * 2;
    const std::size_t bytes = want * sizeof(T);
    // The block is still the top of the bump: move the bump, keep the pointer.
    if (_data != nullptr &&
        a.try_extend(static_cast<void*>(_data), std::size_t{_cap} * sizeof(T),
                     bytes)) {
      _cap = static_cast<size_type>(want);
      return;
    }
    T* const q = static_cast<T*>(a.carve_bytes(bytes, alignof(T)));
    if (_size != 0) std::memcpy(q, _data, std::size_t{_size} * sizeof(T));
    _data = q;
    _cap = static_cast<size_type>(want);
  }

  // Two, not four: the in-place extension is nearly free, so a bigger first
  // block would only buy peak memory.
  static constexpr std::size_t kFirst = 2;

  T* _data = nullptr;
  size_type _size = 0;
  size_type _cap = 0;
};
// Pinned for a 64-bit ABI, and the guard is what makes it portable rather than
// parochial: on wasm32 a pointer is four bytes, so each pin would be a
// different number saying the same thing. What a pin catches is a field
// added. `CstNode`'s own pin needs no guard: it is made of `uint32` and says
// the same on every width.
static_assert(sizeof(void*) != 8 || sizeof(vector<int>) == 16,
              "a list member is a pointer and two 32-bit counts");

// The runtime identity of one AST class, and the identity is the address:
// `&ast_type_tag<T>` is one inline variable per T, unique in the process. Never
// copy a tag or compare tags by value -- the name is decoration, and two
// classes may print the same last component.
//
// Whether a node is a T is answered here, not by the grammar: a tag carries
// the transitive closure of its own bases, which `cursiv::Extends` states and
// a consteval walk closes. A grammar's type list answers the downward
// question -- every subtype of T -- and keeps it.
struct AstNode;

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
// A member called `name`, and plain text: a node, a reference or a list that
// happens to share the word is not a name.
template <class T>
concept has_name_member = requires(const T& t) {
  { t.name } -> std::same_as<const std::string_view&>;
};

template <class T>
consteval std::string_view (*name_reader() noexcept)(const AstNode&) noexcept {
  if constexpr (has_name_member<T>)
    return +[](const AstNode& n) noexcept -> std::string_view {
      return static_cast<const T&>(n).name;
    };
  else
    return nullptr;
}
}  // namespace detail

namespace detail {
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
    detail::bases_of<T>::kTags.data(), detail::type_name<T>(),
    detail::name_reader<T>()};

namespace detail {

// `Extends<Bs...>` states the direct bases; the closure is their tags plus
// theirs, deduplicated -- inheritance is a DAG, so one base may be reached by
// two paths. An upper bound counts each path, the fill keeps each tag once.
template <class T>
consteval std::size_t base_bound() {
  if constexpr (requires { typename T::ast_bases; })
    return []<class... Bs>(TypeList<Bs...>) consteval {
      return (std::size_t{0} + ... + (1 + base_bound<Bs>()));
    }(typename T::ast_bases{});
  else
    return 0;
}

template <class T, std::size_t N>
constexpr void fill_bases(std::array<const AstTypeTag*, N>& out,
                          std::size_t& used) {
  if constexpr (requires { typename T::ast_bases; }) {
    [&]<class... Bs>(TypeList<Bs...>) constexpr {
      const auto one = [&]<class B>() constexpr {
        const AstTypeTag* const tag = &ast_type_tag<B>;
        for (std::size_t at = 0; at < used; ++at)
          if (out[at] == tag) return;
        out[used++] = tag;
        fill_bases<B>(out, used);
      };
      (one.template operator()<Bs>(), ...);
    }(typename T::ast_bases{});
  }
}

template <class T>
struct bases_of {
  static constexpr std::size_t kBound = base_bound<T>();
  static constexpr auto kBuilt = []() consteval {
    std::array<const AstTypeTag*, kBound == 0 ? 1 : kBound> out{};
    std::size_t used = 0;
    fill_bases<T>(out, used);
    return std::pair{out, used};
  }();
  static constexpr std::size_t kCount = kBuilt.second;
  // Trimmed to what was actually found: the bound counts every path, the fill
  // keeps each tag once, and only the kept ones are worth a relocation.
  static constexpr auto kTags = []() consteval {
    std::array<const AstTypeTag*, kCount == 0 ? 1 : kCount> out{};
    for (std::size_t at = 0; at < kCount; ++at) out[at] = kBuilt.first[at];
    return out;
  }();
};

}  // namespace detail

struct Document;

struct AstNode {
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

  // No owned text in the vocabulary: a member holds a `string_view` into the
  // document. A materialised value is allocated by the converter in the
  // resource the builder hands it, and reaches the member as a view of that.
  template <class T>
  using vector = cursiv::vector<T>;
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
static_assert(sizeof(void*) != 8 || sizeof(AstNode) == 24);

// A node declares its base, because C++ cannot be asked for one: the deduced
// type of an alternation is the most derived base its branches share.
template <class... Bs>
struct Extends : Bs... {
  using ast_bases = TypeList<Bs...>;
};

namespace detail {
struct tail_padding_probe : AstNode {
  std::uint32_t first;
};
static_assert(
    sizeof(void*) != 8 || sizeof(tail_padding_probe) == 24,
    "a derived class's first <=4-byte member lives in the base's "
    "tail padding; if this grew, cstNodeId is no longer declared last");
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
  // Overload-set form: the targets land in storage the home owns, and
  // `out` names them.
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

}  // namespace detail

// A cross-reference as an AST field: the name now, one target of Ts... once
// read. `which` is the index in Ts... written by the linker, and reading
// resolves, so a reader needs nothing but the field.
//
// The name is always borrowed -- a slice of the document, or bytes a
// converter allocated in the tree's resource -- so `refText` is a
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
  // Written by a read (`resolve`), hence mutable.
  mutable AstNode* target = nullptr;
  // Installed once by the document over the build's work list; null on a
  // hand-built or detached tree, where reading resolves nothing. The one
  // embedded context pointer, since a read must work with nothing in hand.
  const RefHome* home = nullptr;
  // The CST node the name was read from. Declared after the pointers so it and
  // the three below share one tail word instead of opening a hole.
  NodeId refNodeId = kNoNode;
  std::uint32_t handle = 0;  // rank in the document's work list; keys `home`
  mutable RefState _state = RefState::Unresolved;
  mutable std::uint8_t which = 0;
  // The writing site's narrowing, as an index into this field's
  // `RefOps::narrows`. Written once by the builder before the reference is
  // reachable, never by a resolution, so it needs no atomic read.
  std::uint8_t narrow = kNoNarrow;

  [[nodiscard]] RefState resolve() const {
    const RefState current =
        std::atomic_ref<RefState>(_state).load(std::memory_order_acquire);
    if (current == RefState::Resolved) return current;
    if (home != nullptr &&
        (current == RefState::Unresolved || detail::ref_running(current)))
      return detail::resolve_one_slow(_state, home, handle, target, which);
    return detail::public_ref_state(current);
  }
  RefState state() const { return resolve(); }

  // One candidate needs no type argument.
  detail::pack_at_t<0, Ts...>* get() const
    requires(sizeof...(Ts) == 1)
  {
    return resolve() == RefState::Resolved
               ? static_cast<detail::pack_at_t<0, Ts...>*>(target)
               : nullptr;
  }

  template <class T>
    requires((std::is_same_v<T, Ts> || ...))
  T* get() const {
    if (resolve() != RefState::Resolved) return nullptr;
    constexpr std::uint8_t idx = detail::branch_index<T, Ts...>;
    return which == idx ? static_cast<T*>(target) : nullptr;
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

  // The writer hands over the slice it already had and promises it outlives the
  // tree. `nar` is defaulted for a caller binding a name by hand.
  void bind(NodeId at, AstNode::string_view t, std::uint8_t nar = kNoNarrow) {
    refNodeId = at;
    refText = t;
    narrow = nar;
    _state = RefState::Unresolved;
  }
  // An owning temporary is refused where it is written rather than dangling
  // silently; it wins the overload as an exact match. A static_assert body and
  // not `= delete("...")`, whose reason g++ drops across a module boundary.
  template <detail::owning_text_rvalue S>
  void bind(NodeId, S&&, std::uint8_t = kNoNarrow) {
    static_assert(
        detail::dependent_false<S>,
        "this name OWNS its bytes and dies at the end of the full expression, "
        "while the reference keeps a view of them. Bind a view of something "
        "that outlives the tree: a slice of the parsed document, or bytes "
        "copied into the tree's own resource (`cursiv::copy_into(mem, text)`)");
  }
};

// One name resolving to several targets. `items` is filled after the build,
// by a read, in storage the home owns: the node stays trivially destructible.
template <class... Ts>
struct multi_reference {
  static_assert(sizeof...(Ts) >= 1);
  static_assert(sizeof...(Ts) <= 255,
                "a reference has at most 255 candidate types");
  AstNode::string_view refText;  // borrowed, see `reference`
  mutable MultiRefItems items;   // written by a read, see `reference`
  const RefHome* home = nullptr;
  // The CST node of the name, and the tail word it shares -- see `reference`.
  NodeId refNodeId = kNoNode;
  std::uint32_t handle = 0;
  mutable RefState _state = RefState::Unresolved;
  // The site's narrowing -- see `reference::narrow`. Same tail padding, same
  // rules: one family, one law.
  std::uint8_t narrow = kNoNarrow;

  multi_reference() = default;
  multi_reference(const multi_reference&) = default;
  multi_reference(multi_reference&&) = default;
  multi_reference& operator=(const multi_reference&) = default;
  multi_reference& operator=(multi_reference&&) = default;

  // Same rule and same return contract as `reference::resolve`.
  [[nodiscard]] RefState resolve() const {
    const RefState current =
        std::atomic_ref<RefState>(_state).load(std::memory_order_acquire);
    if (current == RefState::Resolved) return current;
    if (home == nullptr ||
        (current != RefState::Unresolved && !detail::ref_running(current)))
      return detail::public_ref_state(current);
    return detail::resolve_all_slow(_state, home, handle, items);
  }
  RefState state() const { return resolve(); }

  // Read items only after acquiring a published result. A cycle or a detached
  // unresolved reference has no readable overload set.
  std::size_t size() const {
    return resolve() == RefState::Resolved ? items.size() : 0u;
  }
  bool empty() const { return size() == 0; }

  detail::pack_at_t<0, Ts...>* get(std::size_t i) const
    requires(sizeof...(Ts) == 1)
  {
    if (resolve() != RefState::Resolved || i >= items.size()) return nullptr;
    return static_cast<detail::pack_at_t<0, Ts...>*>(items[i].target);
  }

  // The i-th target if it is a T, as `reference::get<T>` answers.
  template <class T>
    requires((std::is_same_v<T, Ts> || ...))
  T* get(std::size_t i) const {
    if (resolve() != RefState::Resolved || i >= items.size())
      return nullptr;
    constexpr std::uint8_t idx = detail::branch_index<T, Ts...>;
    return items[i].which == idx ? static_cast<T*>(items[i].target) : nullptr;
  }

  void bind(NodeId at, AstNode::string_view t, std::uint8_t nar = kNoNarrow) {
    items.clear();
    refNodeId = at;
    refText = t;
    narrow = nar;
    _state = RefState::Unresolved;
  }
  // The same refusal as `reference::bind`, by the same mechanism -- see it
  // for why a `static_assert` body and not `= delete("...")`.
  template <detail::owning_text_rvalue S>
  void bind(NodeId, S&&, std::uint8_t = kNoNarrow) {
    static_assert(
        detail::dependent_false<S>,
        "this name OWNS its bytes and dies at the end of the full expression, "
        "while the reference keeps a view of them. Bind a view of something "
        "that outlives the tree: a slice of the parsed document, or bytes "
        "copied into the tree's own resource (`cursiv::copy_into(mem, text)`)");
  }
};

// Pinned: the shared tail word is what makes `refNodeId`/`handle`/`_state`/
// `which`/`narrow` free, and the pin is what proves a new byte lands in it.
namespace detail {
struct ref_size_probe : AstNode {};
static_assert(sizeof(void*) != 8 || sizeof(reference<ref_size_probe>) == 48);
static_assert(sizeof(void*) != 8 ||
              sizeof(multi_reference<ref_size_probe>) == 56);
}  // namespace detail

// The linker's work list entry. Never a direct pointer to the reference object:
// a vector<reference<T>> relocates its elements as it grows, while the owner is
// arena-stable and the indices are frozen when the build ends.
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

  void (*reset)(AstNode& owner, std::uint32_t index) noexcept = nullptr;
};

namespace detail {
// `DocumentReferences::forceAll` as one phase: a wait anywhere under it is
// cancelled with `token`.
void force_all(std::span<const RefHandle> handles, const RefOps* ops,
               const CancellationToken& token);
}  // namespace detail

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
             ? static_cast<const T*>(node)
             : nullptr;
}

}  // namespace cursiv
