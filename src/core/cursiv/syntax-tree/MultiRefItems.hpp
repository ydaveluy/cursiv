// The targets a name resolving to several has: a view of storage the
// reference's home fills after the build and owns, so the node owns nothing.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cursiv {

struct AstNode;

struct MultiRefItem {
  AstNode* target = nullptr;
  std::uint8_t which = 0;
};

struct MultiRefItems {
  const MultiRefItem* data = nullptr;
  std::uint32_t count = 0;

  std::size_t size() const noexcept { return count; }
  bool empty() const noexcept { return count == 0; }
  const MultiRefItem* begin() const noexcept { return data; }
  const MultiRefItem* end() const noexcept { return data + count; }
  const MultiRefItem& front() const noexcept { return data[0]; }
  const MultiRefItem& operator[](std::size_t i) const noexcept {
    return data[i];
  }
  void clear() noexcept {
    data = nullptr;
    count = 0;
  }
};

}  // namespace cursiv
