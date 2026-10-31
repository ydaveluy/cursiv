// The cursors a compiled predicate carries: where it reads, where its gap
// began, and the reading it holds there. Recognition steps sets of them.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// A cursor's `held` before any reading at `at`; `kInvalidSymbol` is a reading
// that found no token there.
inline constexpr SymbolId kUnread = kInvalidSymbol - 1u;

struct PredicateCursor {
  TextOffset at = 0;
  TextOffset from = 0;
  SymbolId held = kUnread;
  std::uint16_t mode = 0;
  TextOffset end = 0;
  auto operator<=>(const PredicateCursor&) const = default;
};

// Deterministic recognition stays in the inline cursor; only competing
// readings allocate, and duplicate cursors never multiply the next step.
// The size is told by the tail and whether the inline cursor is taken, so a
// moved-from set (its tail emptied, its inline cursor copied) still holds
// what it says: never an index past its storage.
template <class Cursor>
class PredicateStates {
 public:
  PredicateStates() = default;
  explicit PredicateStates(Cursor cursor) : _one(cursor), _taken(true) {}

  bool empty() const { return !_taken; }
  std::size_t size() const { return (_taken ? 1u : 0u) + _more.size(); }
  Cursor operator[](std::size_t i) const {
    return i == 0 ? _one : _more[i - 1];
  }
  bool has(Cursor cursor) const {
    if (!_taken) return false;
    if (_one == cursor) return true;
    return !_more.empty() && !(_more.back() < cursor) &&
           std::binary_search(_more.begin(), _more.end(), cursor);
  }
  void add(Cursor cursor) {
    if (!_taken) {
      _one = cursor;
      _taken = true;
    } else {
      if (_one == cursor) return;
      if (_more.empty() || _more.back() < cursor) {
        _more.push_back(cursor);
      } else {
        const auto at = std::lower_bound(_more.begin(), _more.end(), cursor);
        if (at != _more.end() && *at == cursor) return;
        _more.insert(at, cursor);
      }
    }
  }
  void unite(const PredicateStates& other) {
    if (this == &other || other.empty()) return;
    if (empty()) {
      *this = other;
      return;
    }
    add(other._one);
    if (other._more.empty()) return;
    // Both tails are sorted. Inserting one interleaved endpoint at a time
    // shifts a growing tail quadratically; merge them in a single pass, from
    // where the other's first cursor falls: the cursors before it stay in
    // place, so a set that grows at its end is never copied whole.
    const auto from =
        std::lower_bound(_more.begin(), _more.end(), other._more.front()) -
        _more.begin();
    const auto mid = static_cast<std::ptrdiff_t>(_more.size());
    _more.insert(_more.end(), other._more.begin(), other._more.end());
    std::inplace_merge(_more.begin() + from, _more.begin() + mid, _more.end());
    _more.erase(std::unique(_more.begin() + from, _more.end()), _more.end());
    const auto own = std::lower_bound(_more.begin() + from, _more.end(), _one);
    if (own != _more.end() && *own == _one) _more.erase(own);
  }
  PredicateStates without(const PredicateStates& other) const {
    PredicateStates out;
    for (std::size_t i = 0, n = size(); i < n; ++i)
      if (!other.has((*this)[i])) out.add((*this)[i]);
    return out;
  }
  bool operator==(const PredicateStates& other) const {
    if (size() != other.size()) return false;
    for (std::size_t i = 0, n = size(); i < n; ++i)
      if (!other.has((*this)[i])) return false;
    return true;
  }

 private:
  Cursor _one{};
  bool _taken = false;
  std::vector<Cursor> _more;
};

using PredicateState = PredicateStates<PredicateCursor>;

}  // namespace cursiv
