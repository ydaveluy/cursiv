// Where the byte classes of the pools were interned: a class is told by its
// bytes, the lead bytes it decodes at and the scalars it lists, and the pool is
// filled in interning order, so the index names the pool's own indices.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/shared/gen/set_dedup.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv::detail {

// A set's identity: its bytes, refused outright; the set of lead bytes at
// which the word class's scalar decides instead -- `kNoSet` for a plain class;
// and where the scalars it tests are listed, zero for the word class's own. A
// set built from the word class keeps its second half through every union and
// intersection it enters, so a reader decodes exactly where the class does and
// never under a plain class of the same bytes.
struct SetKey {
  ByteSet bytes;
  std::uint16_t decode = kNoSet;
  std::uint32_t scalars = 0;
  friend constexpr bool operator==(const SetKey&, const SetKey&) = default;
};
struct SetKeyHash {
  std::size_t operator()(const SetKey& k) const {
    return static_cast<std::size_t>(
        hash_mix(hash_mix(set_hash(k.bytes), k.decode), k.scalars));
  }
};

// The index over a pool of sets. It follows the pool rather than every writer:
// `sync` reads what the pool holds past the index, and forgets what a trial
// cut the pool back from, so a lookup answers for the pool as it stands.
struct SetIndex {
  std::unordered_map<SetKey, std::uint16_t, SetKeyHash> at{};
  // The key of each set, in the pool's order.
  std::vector<SetKey> keys{};

  template <class Cfg>
  void sync(const Cfg& c) {
    while (keys.size() > c.nsets) {
      at.erase(keys.back());
      keys.pop_back();
    }
    for (std::size_t i = keys.size(); i < c.nsets; ++i)
      add(SetKey{c.sets[i], c.setDecode[i],
                 i < c.setScalars.size() ? c.setScalars[i] : 0u});
  }

  // Names the next set of the pool; the first of a key stands for it.
  void add(const SetKey& key) {
    at.try_emplace(key, static_cast<std::uint16_t>(keys.size()));
    keys.push_back(key);
  }

  const std::uint16_t* find(const SetKey& key) const {
    const auto hit = at.find(key);
    return hit == at.end() ? nullptr : &hit->second;
  }
};

}  // namespace cursiv::detail
