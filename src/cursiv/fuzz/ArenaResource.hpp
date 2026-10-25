// The tree's own bump allocator, as the memory_resource a converter asks for.
// Its deallocate is a no-op: bytes live exactly as long as the arena, so a
// view it returns outlives every reader of the tree.
#pragma once

#include <cstddef>
#include <memory_resource>

#include "cursiv/core/syntax-tree/AstArena.hpp"

namespace cursiv::fuzzer {

class ArenaResource final : public std::pmr::memory_resource {
 public:
  explicit ArenaResource(AstArena& ar) : _ar(&ar) {}

 private:
  void* do_allocate(std::size_t bytes, std::size_t align) override {
    return _ar->carve_bytes(bytes, align);
  }
  void do_deallocate(void*, std::size_t, std::size_t) override {}
  [[nodiscard]] bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  AstArena* _ar;
};

}  // namespace cursiv::fuzzer
