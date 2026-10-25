#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cursiv {

// The names a document looked for past itself, as folded hashes: a collision
// only relinks more. Recorded from any worker that resolves the document.
class ImportedNames {
 public:
  void record(std::uint64_t hash) {
    auto& recent = _recent[hash & (_recent.size() - 1)];
    if (hash != 0 && recent.load(std::memory_order_relaxed) == hash) return;
    const std::scoped_lock lock(_mutex);
    if (_names.empty() || _names.back() != hash) _names.push_back(hash);
    // A cached hit must name an entry already committed to the collection.
    recent.store(hash, std::memory_order_relaxed);
  }

  [[nodiscard]] std::vector<std::uint64_t> snapshot() const;

  // Invalidation excludes readers; cached hits must not cross this reset.
  void clear();

 private:
  mutable std::mutex _mutex;
  mutable std::vector<std::uint64_t> _names;
  std::array<std::atomic<std::uint64_t>, 64> _recent{};
};

}  // namespace cursiv
