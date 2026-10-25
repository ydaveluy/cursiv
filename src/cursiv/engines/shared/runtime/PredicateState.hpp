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
template <class Cursor>
class PredicateStates {
 public:
  PredicateStates() = default;
  explicit PredicateStates(Cursor cursor) : _one(cursor), _size(1) {}

  bool empty() const { return _size == 0; }
  std::size_t size() const { return _size; }
  Cursor operator[](std::size_t i) const {
    return i == 0 ? _one : _more[i - 1];
  }
  bool has(Cursor cursor) const {
    if (_size == 0) return false;
    if (_one == cursor) return true;
    return !_more.empty() && !(_more.back() < cursor) &&
           std::binary_search(_more.begin(), _more.end(), cursor);
  }
  void add(Cursor cursor) {
    if (_size == 0) {
      _one = cursor;
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
    ++_size;
  }
  void unite(const PredicateStates& other) {
    for (std::size_t i = 0; i < other.size(); ++i) add(other[i]);
  }
  PredicateStates without(const PredicateStates& other) const {
    PredicateStates out;
    for (std::size_t i = 0; i < _size; ++i)
      if (!other.has((*this)[i])) out.add((*this)[i]);
    return out;
  }
  bool operator==(const PredicateStates& other) const {
    if (_size != other._size) return false;
    for (std::size_t i = 0; i < _size; ++i)
      if (!other.has((*this)[i])) return false;
    return true;
  }

 private:
  Cursor _one{};
  std::size_t _size = 0;
  std::vector<Cursor> _more;
};

using PredicateState = PredicateStates<PredicateCursor>;

}  // namespace cursiv
