// A walk's view of a frozen stack: the frames stay where they are and only
// what the walk pushes sits beside them. Copying it copies the pushes, never
// the frames.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/lr/runtime/LrStack.hpp"

namespace cursiv::engine {

class LrStackView {
 public:
  // Stands on `s` anew; the tail keeps its storage.
  void stand_on(const LrStack& s) {
    _base = &s;
    _kept = s.size();
    _tail.clear();
  }
  [[nodiscard]] std::uint32_t size() const {
    return _kept + static_cast<std::uint32_t>(_tail.size());
  }
  // The frames below still standing.
  [[nodiscard]] std::uint32_t kept() const { return _kept; }
  [[nodiscard]] std::uint32_t state_at(std::uint32_t i) const {
    return i < _kept ? (*_base)[i].state : _tail[i - _kept];
  }
  [[nodiscard]] std::uint32_t back() const {
    return !_tail.empty() ? _tail.back() : (*_base)[_kept - 1].state;
  }
  void push(std::uint32_t state) { _tail.push_back(state); }
  // A reduction pops the handle it closes, from the tail first and the frames
  // under it after: the walk never rewrites what it stands on.
  void pop(std::uint32_t n) {
    const auto fromTail =
        n < _tail.size() ? n : static_cast<std::uint32_t>(_tail.size());
    _tail.resize(_tail.size() - fromTail);
    _kept -= n - fromTail;
  }

 private:
  const LrStack* _base = nullptr;
  std::uint32_t _kept = 0;
  std::vector<std::uint32_t> _tail;
};

// Two stack views are one where the tops are the same state and every frame
// below has the same class, `klass` naming a state's class. Two that popped to
// the same shared depth agree on everything below.
template <class Class>
inline bool lr_same_run(const LrStackView& a, const LrStackView& b,
                        Class&& klass) {
  if (a.size() != b.size()) return false;
  for (std::uint32_t i = a.size(); i-- > 0;) {
    if (i < a.kept() && i < b.kept()) return true;
    const std::uint32_t x = a.state_at(i);
    const std::uint32_t y = b.state_at(i);
    if (i + 1 == a.size() ? x != y : klass(x) != klass(y)) return false;
  }
  return true;
}

}  // namespace cursiv::engine
