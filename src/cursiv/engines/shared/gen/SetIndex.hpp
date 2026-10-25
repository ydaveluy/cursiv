// Where the byte classes of a token graph were interned: a class is told by
// its bytes and the lead bytes it decodes at, and the pool is filled in
// interning order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/set_dedup.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv::detail {

// A set's identity: its bytes, refused outright, and the set of lead bytes
// at which the word class's scalar decides instead -- `kNoSet` for a plain
// class. A set built from the word class keeps that second half through
// every union and intersection it enters, so a reader decodes exactly where
// the class does and never under a plain class of the same bytes.
struct SetKey {
  ByteSet bytes;
  std::uint16_t decode = kNoSet;
  friend constexpr bool operator==(const SetKey&, const SetKey&) = default;
};
struct SetKeyHash {
  std::size_t operator()(const SetKey& k) const {
    return static_cast<std::size_t>(hash_mix(set_hash(k.bytes), k.decode));
  }
};

// Where a set was interned. The pool is asked once per literal byte of the
// grammar, and it is filled in interning order: the indices are the indices.
struct SetIndex : std::unordered_map<SetKey, std::uint16_t, SetKeyHash> {
  void bind(const void* owner) {
    if (_owner == owner) return;
    clear();
    _owner = owner;
  }

 private:
  const void* _owner = nullptr;
};

template <class Cfg>
std::uint16_t intern_set(Cfg& c, SetIndex& x, const ByteSet& s,
                         std::uint16_t decode = kNoSet) {
  x.bind(&c);
  const auto [at, fresh] = x.try_emplace(SetKey{s, decode}, c.nsets);
  if (fresh) {
    if (c.nsets == kNoSet) throw LowerRefused(LowerFail::tokenGraphTooLarge);
    c.sets[c.nsets] = s;
    c.setDecode[c.nsets] = decode;
    ++c.nsets;
  }
  return at->second;
}

}  // namespace cursiv::detail
