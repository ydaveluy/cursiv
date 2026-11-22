#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <type_traits>

#include "cursiv/syntax-tree/AstNode.hpp"

// AST nodes are bump-allocated from the memory resource the CST already owns,
// so one release frees both. `ParseResult` runs each node's destructor to free
// what its members own.

namespace cursiv::detail {

class AstArena {
 public:
  explicit AstArena(std::pmr::memory_resource* mem) : _mem(mem) {}
  AstArena(const AstArena&) = delete;
  AstArena& operator=(const AstArena&) = delete;
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

 private:
  // Nodes are allocated from chunks of the pool, which owns them and frees
  // them all at once. A small chunk lets a small document's nodes share the
  // block its records came from.
  static constexpr std::size_t kChunk = static_cast<std::size_t>(16u * 1024u);

  // Eight, not `max_align_t`'s sixteen: every node size is rounded up to this,
  // and sixteen would pad about half of the AST classes.
  static constexpr std::size_t kBumpAlign = 8u;

  [[gnu::cold, gnu::noinline]] void refill() {
    _cur = static_cast<char*>(_mem->allocate(kChunk, kBumpAlign));
    _end = _cur + kChunk;
  }

  template <class T>
  void* allocate_for() {
    if constexpr (alignof(T) > kBumpAlign || sizeof(T) > kChunk) {
      return _mem->allocate(sizeof(T), alignof(T));
    } else {
      constexpr std::size_t size = (sizeof(T) + kBumpAlign - 1) & ~(kBumpAlign - 1);
      if (static_cast<std::size_t>(_end - _cur) < size) [[unlikely]] refill();
      char* const node = _cur;
      _cur += size;
      return node;
    }
  }

  std::pmr::memory_resource* _mem = nullptr;
  char* _cur = nullptr;
  char* _end = nullptr;
};

}  // namespace cursiv::detail
