// The memory a tree and the AST built from it are carved from, freed at once.
#pragma once

#include <cstddef>
#include <memory_resource>

namespace cursiv::detail {

// Blocks grow from `first` bytes to a cap and then keep that size; a request
// larger than half the cap gets a block of its own, rounded up to a power of
// two when one can hold it. A request too large for a block's header beside
// it throws `std::bad_alloc`. A document that keeps growing therefore finds, at
// each reparse, freed blocks of the sizes it asks for again. Wasm linear memory
// never shrinks and reuses a freed range only for a request that fits in it, so
// a pool whose blocks keep doubling would leave each smaller one behind until
// the memory limit is reached.
class BlockArena final : public std::pmr::memory_resource {
 public:
  explicit BlockArena(std::size_t first, std::pmr::memory_resource* upstream =
                                             std::pmr::new_delete_resource())
      : _first(first), _next(first), _upstream(upstream) {}
  BlockArena(const BlockArena&) = delete;
  BlockArena& operator=(const BlockArena&) = delete;
  ~BlockArena() override { release(); }

  // Every block back to the upstream resource.
  void release() noexcept;

  static constexpr std::size_t kCap = std::size_t{4} << 20;

 private:
  struct Block {
    Block* prev;
    std::size_t size;
    std::size_t align;
  };

  void* do_allocate(std::size_t bytes, std::size_t align) override;
  void do_deallocate(void*, std::size_t, std::size_t) noexcept override {}
  [[nodiscard]] bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
  Block* take(std::size_t size, std::size_t align);

  std::size_t _first;
  std::size_t _next;
  std::pmr::memory_resource* _upstream;
  Block* _blocks = nullptr;
  char* _at = nullptr;
  char* _end = nullptr;
};

}  // namespace cursiv::detail
