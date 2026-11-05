#include "cursiv/syntax-tree/BlockArena.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <new>

namespace cursiv::detail {

namespace {

char* aligned(char* at, std::size_t align) {
  const auto bits = reinterpret_cast<std::uintptr_t>(at);
  return at + ((align - bits % align) % align);
}

}  // namespace

BlockArena::Block* BlockArena::take(std::size_t size, std::size_t align) {
  const std::size_t blockAlign = std::max(align, alignof(Block));
  auto* const block =
      static_cast<Block*>(_upstream->allocate(size, blockAlign));
  *block = Block{_blocks, size, blockAlign};
  _blocks = block;
  return block;
}

void* BlockArena::do_allocate(std::size_t bytes, std::size_t align) {
  if (char* const at = aligned(_at, align);
      _at != nullptr && at + bytes <= _end) {
    _at = at + bytes;
    return at;
  }
  const std::size_t room = sizeof(Block) + align + bytes;
  if (room > kCap / 2) {
    // Its own block: the current one keeps carving the small requests.
    Block* const block = take(std::bit_ceil(room), align);
    return aligned(reinterpret_cast<char*>(block + 1), align);
  }
  const std::size_t size = std::max(_next, std::bit_ceil(room));
  _next = std::min(size * 2, kCap);
  Block* const block = take(size, align);
  char* const at = aligned(reinterpret_cast<char*>(block + 1), align);
  _at = at + bytes;
  _end = reinterpret_cast<char*>(block) + size;
  return at;
}

void BlockArena::release() noexcept {
  while (_blocks != nullptr) {
    Block* const prev = _blocks->prev;
    _upstream->deallocate(_blocks, _blocks->size, _blocks->align);
    _blocks = prev;
  }
  _at = _end = nullptr;
  _next = _first;
}

}  // namespace cursiv::detail
