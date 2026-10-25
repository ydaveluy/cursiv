#include "cursiv/core/references/ImportedNames.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cursiv {

std::vector<std::uint64_t> ImportedNames::snapshot() const {
  const std::scoped_lock lock(_mutex);
  std::ranges::sort(_names);
  _names.erase(std::ranges::unique(_names).begin(), _names.end());
  return _names;
}

void ImportedNames::clear() {
  const std::scoped_lock lock(_mutex);
  _names.clear();
  for (auto& recent : _recent) recent.store(0, std::memory_order_relaxed);
}

}  // namespace cursiv
