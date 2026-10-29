#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>

#include "cursiv/core/syntax-tree/AstNode.hpp"

// Where the AST nodes live: a bump over the resource the tree already owns,
// so one release frees both. A node owns nothing and runs no destructor:
// the arena is the pool's bump and nothing else.

namespace cursiv {

class AstArena {
 public:
  AstArena() = default;
  explicit AstArena(std::pmr::memory_resource* mem) : _mem(mem) {}
  // The chunk moves with the pool it came from and the source keeps none: a
  // defaulted move would copy `_cur`/`_end`, leaving two arenas bumping the
  // same block and handing one address to two nodes.
  AstArena(AstArena&& src) noexcept { *this = std::move(src); }
  AstArena& operator=(AstArena&& src) noexcept {
    _mem = src._mem;
    // The chunk belongs to the incoming `_mem`: taking one without the other
    // would carve the new pool's nodes out of the old pool's block.
    _cur = src._cur;
    _end = src._end;
    src._cur = nullptr;
    src._end = nullptr;
    return *this;
  }

  template <class T>
  T* make() {
    static_assert(std::is_base_of_v<AstNode, T>,
                  "every AST node derives from cursiv::AstNode");
    static_assert(std::is_trivially_destructible_v<T>,
                  "an AST node owns nothing: its lists are the arena's and a "
                  "plural reference's targets are its home's, so the tree is "
                  "freed as one block and no destructor ever runs");
    // A default-constructed arena borrows nothing: `make` on it would
    // dereference null rather than say what is missing.
    assert(_mem != nullptr && "AstArena needs a memory resource before make()");
    void* s = carve_for<T>();
    T* v = std::construct_at(static_cast<T*>(s));
    // The type tag is written at construction, here and nowhere else, so a node
    // built by hand carries the same identity a parsed one does.
    AstNode* const base = v;
    base->type = &ast_type_tag<T>;
    return v;
  }

  // `cursiv::vector` seats its storage here. It names no arena type: the two
  // calls below are the whole contract, taken as a template parameter.

  // Untyped storage from the same bump the nodes come out of.
  void* carve_bytes(std::size_t size, std::size_t align) {
    assert(_mem != nullptr &&
           "AstArena needs a memory resource before a carve");
    return carve(size, align);
  }

  // Grows the last carve without moving it, which is what makes a list member
  // cost its exact bytes: the appends to one member form a contiguous run, so
  // its block is still the top of the bump. False when something was carved in
  // between, and the caller then copies once.
  bool try_extend(void* p, std::size_t oldBytes, std::size_t newBytes) {
    char* const q = static_cast<char*>(p);
    if (q + oldBytes != _cur) return false;
    const std::size_t more = newBytes - oldBytes;
    if (std::cmp_greater(more, _end - _cur)) return false;
    _cur = align_up(q + newBytes, kBumpAlign);
    if (_cur > _end) _cur = _end;
    return true;
  }

  // Forgets every node: the arena stays usable but every node it built is
  // gone. The chunk goes too: a `_cur` kept across this would carve the next
  // node out of memory the pool has taken back.
  void release() noexcept {
    _cur = nullptr;
    _end = nullptr;
  }

 private:
  // Nodes are carved in chunks and not requested one by one: `_mem->allocate`
  // is a virtual call wrapped around what is a pointer increment anyway. The
  // pool stays the owner, so a caller that supplied a resource still frees
  // everything in one piece, and nothing is imposed on a node's own members.
  // The arena never releases a single node, so interior pointers are sound.
  static constexpr std::size_t kChunk = static_cast<std::size_t>(64u * 1024u);

  // Eight rather than `max_align_t`'s sixteen: every node's size is rounded up
  // to this, and an ordinary AST class is already a multiple of eight, where
  // sixteen padded half of them for nothing.
  static constexpr std::size_t kBumpAlign = 8u;

  static char* align_up(const char* p, std::size_t align) {
    const auto u = reinterpret_cast<std::uintptr_t>(p);
    const auto a = std::uintptr_t{align};
    return reinterpret_cast<char*>((u + a - 1u) & ~(a - 1u));
  }

  // The slow half, out of line: one call per chunk, not per node.
  [[gnu::cold, gnu::noinline]] void* carve_chunk(std::size_t size,
                                                 std::size_t align) {
    if (size > std::numeric_limits<std::size_t>::max() - align)
      throw std::bad_alloc();
    // A node wider than a chunk gets its own exact block; the current chunk
    // keeps its remaining room for the nodes that follow.
    const std::size_t want = size + align > kChunk ? size + align : kChunk;
    char* const block = static_cast<char*>(_mem->allocate(want, kBumpAlign));
    char* const p = align_up(block, align);
    // An oversized node takes its own block and the current chunk is kept:
    // dropping it would strand the room left for the nodes that follow.
    if (want == kChunk) {
      _cur = align_up(p + size, kBumpAlign);
      _end = block + want;
    }
    return p;
  }

  // The room is a distance and not a pointer: `p + size > _end` would form a
  // pointer past one-past-the-end whenever the chunk is exhausted, and on the
  // first node, where both are null. `_cur` stays `kBumpAlign`-aligned on every
  // path, which is the premise of `carve_for` below.
  void* carve(std::size_t size, std::size_t align) {
    char* const p = align_up(_cur, align);
    // The align-up can land past the chunk, which would make the distance
    // negative: that is exhaustion too.
    if (p > _end || std::cmp_greater(size, _end - p)) [[unlikely]]
      return carve_chunk(size, align);
    char* const next = align_up(p + size, kBumpAlign);
    // Rounding up must not walk the pointer past one-past-the-end of the
    // block; the chunk simply ends here when it would.
    _cur = next > _end ? _end : next;
    return p;
  }

  // Size and alignment are compile-time here, and `_cur` is held at
  // `kBumpAlign`, so the align-up disappears for every ordinary AST class and
  // the hot path is a load, a constant add, a compare and a store. An
  // over-aligned node fails the test and takes the general path.
  template <class T>
  void* carve_for() {
    if constexpr (alignof(T) <= kBumpAlign) {
      constexpr std::size_t n =
          (sizeof(T) + kBumpAlign - 1u) & ~(kBumpAlign - 1u);
      char* const p = _cur;
      if (std::cmp_greater(n, _end - p)) [[unlikely]]
        return carve_chunk(n, kBumpAlign);
      _cur = p + n;
      return p;
    } else {
      return carve(sizeof(T), alignof(T));
    }
  }

  std::pmr::memory_resource* _mem = nullptr;
  char* _cur = nullptr;
  char* _end = nullptr;
};

}  // namespace cursiv
