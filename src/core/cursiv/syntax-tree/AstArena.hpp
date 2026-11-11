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

#include "cursiv/syntax-tree/AstNode.hpp"

// AST nodes are bump-allocated from the memory resource the CST already owns,
// so one release frees both. `ParseResult` runs each node's destructor to free
// what its members own.

namespace cursiv::detail {

class AstArena {
 public:
  AstArena() = default;
  explicit AstArena(std::pmr::memory_resource* mem) : _mem(mem) {}
  // The source gives up its chunk: a defaulted move would leave two arenas
  // allocating from the same block.
  AstArena(AstArena&& src) noexcept { *this = std::move(src); }
  AstArena& operator=(AstArena&& src) noexcept {
    _mem = src._mem;
    // The chunk comes from `src._mem`, so it moves with it.
    _cur = src._cur;
    _end = src._end;
    src._cur = nullptr;
    src._end = nullptr;
    return *this;
  }

  template <class T>
  T* make() {
    static_assert(std::is_base_of_v<AstNode, T>,
                  "an AST node type must derive from cursiv::AstNode");
    assert(_mem != nullptr && "AstArena needs a memory resource before make()");
    void* s = allocate_for<T>();
    T* v = std::construct_at(static_cast<T*>(s));
    // Set the type tag, as `new_ast_node` does, so a node built by hand has the
    // same identity as a parsed one.
    set_type_tag(*v);
    return v;
  }

  // Forgets every node and the current chunk, which the pool may take back.
  // The arena stays usable.
  void release() noexcept {
    _cur = nullptr;
    _end = nullptr;
  }

 private:
  // Nodes are allocated from chunks of the pool, which owns them and frees
  // them all at once. A small chunk lets a small document's nodes share the
  // block its records came from.
  static constexpr std::size_t kChunk = static_cast<std::size_t>(16u * 1024u);

  // Eight, not `max_align_t`'s sixteen: every node size is rounded up to this,
  // and sixteen would pad about half of the AST classes.
  static constexpr std::size_t kBumpAlign = 8u;

  static char* align_up(const char* p, std::size_t align) {
    const auto u = reinterpret_cast<std::uintptr_t>(p);
    const auto a = std::uintptr_t{align};
    return reinterpret_cast<char*>((u + a - 1u) & ~(a - 1u));
  }

  // The slow path, out of line: one call per chunk, not per node.
  [[gnu::cold, gnu::noinline]] void* allocate_chunk(std::size_t size,
                                                    std::size_t align) {
    if (size > std::numeric_limits<std::size_t>::max() - align)
      throw std::bad_alloc();
    // A node wider than a chunk gets its own exact block; the current chunk
    // keeps its remaining room for the nodes that follow.
    const std::size_t want = size + align > kChunk ? size + align : kChunk;
    auto* const block = static_cast<char*>(_mem->allocate(want, kBumpAlign));
    char* const p = align_up(block, align);
    if (want == kChunk) {
      _cur = align_up(p + size, kBumpAlign);
      _end = block + want;
    }
    return p;
  }

  // The room left is compared as a distance, so no pointer goes past the
  // chunk. `_cur` stays `kBumpAlign`-aligned, which `allocate_for` relies on.
  void* allocate(std::size_t size, std::size_t align) {
    char* const p = align_up(_cur, align);
    // The align-up can go past the chunk, which would make the distance
    // negative: that is exhaustion too.
    if (p > _end || std::cmp_greater(size, _end - p)) [[unlikely]]
      return allocate_chunk(size, align);
    char* const next = align_up(p + size, kBumpAlign);
    // Rounding up must not go past the end of the block.
    _cur = next > _end ? _end : next;
    return p;
  }

  // Size and alignment are known here and `_cur` is `kBumpAlign`-aligned, so
  // an ordinary node needs no align-up. An over-aligned one takes `allocate`.
  template <class T>
  void* allocate_for() {
    if constexpr (alignof(T) <= kBumpAlign) {
      constexpr std::size_t n =
          (sizeof(T) + kBumpAlign - 1u) & ~(kBumpAlign - 1u);
      char* const p = _cur;
      if (std::cmp_greater(n, _end - p)) [[unlikely]]
        return allocate_chunk(n, kBumpAlign);
      _cur = p + n;
      return p;
    } else {
      return allocate(sizeof(T), alignof(T));
    }
  }

  std::pmr::memory_resource* _mem = nullptr;
  char* _cur = nullptr;
  char* _end = nullptr;
};

}  // namespace cursiv::detail
