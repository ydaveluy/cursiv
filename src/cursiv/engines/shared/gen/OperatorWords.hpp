// The token strings an operator can spell, one terminal id per token. Two
// operators on different steps may open on one token only where no string is
// both of theirs: the text after the first token then tells them apart.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cursiv {

class OperatorWords {
 public:
  // Past this many strings the set stops listing them and keeps the tokens
  // they open on.
  static constexpr std::size_t kLimit = 256;

  static OperatorWords none() { return {}; }
  static OperatorWords nothing() {
    OperatorWords w;
    w._words.emplace_back();
    return w;
  }
  static OperatorWords token(std::uint16_t t) {
    OperatorWords w;
    w._words.push_back({t});
    return w;
  }
  // Spells something this set does not list, opening on `opening`.
  static OperatorWords unlisted(std::span<const std::uint16_t> opening) {
    OperatorWords w;
    w._open = true;
    w._opening.assign(opening.begin(), opening.end());
    w.settle();
    return w;
  }

  void unite(const OperatorWords& o) {
    _open = _open || o._open;
    _opening.insert(_opening.end(), o._opening.begin(), o._opening.end());
    _words.insert(_words.end(), o._words.begin(), o._words.end());
    settle();
  }

  OperatorWords then(const OperatorWords& o) const {
    OperatorWords out;
    out._open = _open || o._open;
    out._opening = _opening;
    // An unlisted prefix may spell nothing, and then what follows opens too.
    if (_open) {
      const std::vector<std::uint16_t> theirs = o.opening();
      out._opening.insert(out._opening.end(), theirs.begin(), theirs.end());
    }
    if (o._open)
      for (const auto& w : _words)
        if (!w.empty()) out._opening.push_back(w.front());
    for (const auto& a : _words) {
      if (out._words.size() > kLimit) break;
      for (const auto& b : o._words) {
        auto w = a;
        w.insert(w.end(), b.begin(), b.end());
        out._words.push_back(std::move(w));
      }
    }
    out.settle();
    return out;
  }

  bool meets(const OperatorWords& o) const {
    if (!_open && !o._open) {
      auto a = _words.begin();
      auto b = o._words.begin();
      while (a != _words.end() && b != o._words.end()) {
        if (*a < *b)
          ++a;
        else if (*b < *a)
          ++b;
        else
          return true;
      }
      return false;
    }
    return std::ranges::any_of(
               o._opening, [&](std::uint16_t t) { return opens_on(t); }) ||
           std::ranges::any_of(o._words, [&](const auto& w) {
             return !w.empty() && opens_on(w.front());
           });
  }

 private:
  // `_words` and `_opening` stay sorted: the words by their first token, the
  // empty one first.
  bool opens_on(std::uint16_t t) const {
    return std::ranges::binary_search(_opening, t) ||
           std::ranges::binary_search(
               _words, int{t}, {},
               [](const auto& w) { return w.empty() ? -1 : int{w.front()}; });
  }
  std::vector<std::uint16_t> opening() const {
    std::vector<std::uint16_t> out = _opening;
    for (const auto& w : _words)
      if (!w.empty()) out.push_back(w.front());
    return out;
  }
  void settle() {
    std::ranges::sort(_words);
    _words.erase(std::unique(_words.begin(), _words.end()), _words.end());
    if (_words.size() > kLimit) {
      _open = true;
      _opening = opening();
      _words.clear();
    }
    std::ranges::sort(_opening);
    _opening.erase(std::unique(_opening.begin(), _opening.end()),
                   _opening.end());
  }

  std::vector<std::vector<std::uint16_t>> _words{};
  std::vector<std::uint16_t> _opening{};
  bool _open = false;
};

}  // namespace cursiv
