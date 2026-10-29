// The tokens a position was read again as, off some terminals: the same read
// is asked by every reading that stands there.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

class LrSkipMemo {
 public:
  static constexpr std::uint32_t kSkips = 4;

  // The token read from `from` in `mode` under bracket depth `depth`, off the
  // `n` terminals `skip` names; null for none.
  [[nodiscard]] const Token* find(TextOffset from, std::uint16_t mode,
                                  std::uint32_t depth,
                                  const std::uint16_t* skip,
                                  std::uint32_t n) const {
    if (_slots.empty()) return nullptr;
    const Slot& s = _slots[index(from, mode, skip, n)];
    return s.live && s.from == from && s.mode == mode && s.depth == depth &&
                   same(s, skip, n)
               ? &s.token
               : nullptr;
  }

  void put(TextOffset from, std::uint16_t mode, std::uint32_t depth,
           const std::uint16_t* skip, std::uint32_t n, const Token& token) {
    if (_slots.empty()) _slots.resize(kSlots);
    Slot& s = _slots[index(from, mode, skip, n)];
    s.live = true;
    s.from = from;
    s.mode = mode;
    s.depth = depth;
    s.n = static_cast<std::uint8_t>(n);
    for (std::uint32_t i = 0; i < n; ++i) s.skip[i] = skip[i];
    s.token = token;
  }

 private:
  static constexpr std::uint32_t kSlots = 128;
  struct Slot {
    bool live = false;
    std::uint8_t n = 0;
    std::uint16_t mode = 0;
    TextOffset from = 0;
    std::uint32_t depth = 0;
    std::uint16_t skip[kSkips];
    Token token;
  };
  static bool same(const Slot& s, const std::uint16_t* skip, std::uint32_t n) {
    if (s.n != n) return false;
    for (std::uint32_t i = 0; i < n; ++i)
      if (s.skip[i] != skip[i]) return false;
    return true;
  }
  static std::uint32_t index(TextOffset from, std::uint16_t mode,
                              const std::uint16_t* skip, std::uint32_t n) {
    std::uint32_t h = from * 0x9E3779B1u + mode * 0x85EBu;
    for (std::uint32_t i = 0; i < n; ++i) h = (h ^ skip[i]) * 0x9E3779B1u;
    return (h >> 8) % kSlots;
  }
  std::vector<Slot> _slots;
};

}  // namespace cursiv
