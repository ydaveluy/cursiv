#include "cursiv/syntax-tree/BlockArena.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <new>

namespace cursiv::detail {

namespace {

std::size_t padding(const char* at, std::size_t align) {
  const auto bits = reinterpret_cast<std::uintptr_t>(at);
  return (align - bits % align) % align;
}

char* aligned(char* at, std::size_t align) { return at + padding(at, align); }

constexpr std::size_t kLargestPower =
    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);

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
  // Measured as distances, so no pointer past the block's end is formed.
  if (_at != nullptr) {
    const std::size_t pad = padding(_at, align);
    if (const auto left = static_cast<std::size_t>(_end - _at);
        pad <= left && bytes <= left - pad) {
      char* const at = _at + pad;
      _at = at + bytes;
      return at;
    }
  }
  // A block holds its header and the alignment slack beside the bytes.
  if (bytes > std::numeric_limits<std::size_t>::max() - sizeof(Block) - align)
    throw std::bad_alloc();
  const std::size_t room = sizeof(Block) + align + bytes;
  if (room > kCap / 2) {
    // Its own block: the current one keeps serving the small requests.
    Block* const block =
        take(room > kLargestPower ? room : std::bit_ceil(room), align);
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

void BlockArena::do_deallocate(void* p, std::size_t bytes,
                               std::size_t align) noexcept {
  if (sizeof(Block) + align + bytes <= kCap / 2) return;
  for (Block** at = &_blocks; *at != nullptr; at = &(*at)->prev)
    if (aligned(reinterpret_cast<char*>(*at + 1), align) == p) {
      Block* const gone = *at;
      *at = gone->prev;
      _upstream->deallocate(gone, gone->size, gone->align);
      return;
    }
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
