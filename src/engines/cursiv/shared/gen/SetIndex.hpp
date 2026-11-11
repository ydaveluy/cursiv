// Where the byte classes of the pools were interned: a class is identified by
// its bytes, the lead bytes it decodes at and the scalars it lists.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "cursiv/shared/gen/SetPool.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/TermInfo.hpp"

namespace cursiv::engine {

// A set's identity: its bytes; the lead bytes at which a scalar decides
// instead, `kNoSet` for a plain class; and where its scalars are listed, zero
// for the word class's own.
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

// The pool index of each set `lex_intern_set` wrote.
using SetIndex = std::unordered_map<SetKey, std::uint16_t, SetKeyHash>;

}  // namespace cursiv::engine
