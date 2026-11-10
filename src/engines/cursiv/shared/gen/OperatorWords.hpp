// The token strings an operator can spell, one id per token. Two operators on
// different levels may open on one token only where no string is both of
// theirs: the text after the first token then tells them apart.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cursiv::engine {

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
  // Spells strings no set lists, opening where `from` opens.
  static OperatorWords unlisted(const OperatorWords& from) {
    OperatorWords w;
    w._open = true;
    w._opening = from.opening();
    w.normalize();
    return w;
  }

  void unite(const OperatorWords& o) {
    _open = _open || o._open;
    _opening.insert(_opening.end(), o._opening.begin(), o._opening.end());
    _words.insert(_words.end(), o._words.begin(), o._words.end());
    normalize();
  }

  OperatorWords then(const OperatorWords& o) const {
    if ((!_open && _words.empty()) || (!o._open && o._words.empty()))
      return none();
    OperatorWords out;
    const auto keep_openings = [&] {
      out._open = true;
      out._opening = opening();
      // An unlisted prefix may spell nothing, as can a listed empty word.
      if (_open || std::ranges::any_of(
                       _words, [](const auto& w) { return w.empty(); })) {
        const auto theirs = o.opening();
        out._opening.insert(out._opening.end(), theirs.begin(), theirs.end());
      }
    };
    if (_open || o._open) keep_openings();
    for (const auto& a : _words) {
      if (out._words.size() > kLimit) {
        // The listing is partial, but every prefix still belongs to the
        // product. Keep all their openings before discarding the listing.
        keep_openings();
        break;
      }
      for (const auto& b : o._words) {
        auto w = a;
        w.insert(w.end(), b.begin(), b.end());
        out._words.push_back(std::move(w));
      }
    }
    out.normalize();
    return out;
  }

  // Whether the set holds strings it does not list.
  bool open() const { return _open; }

  // A string of this set that one of `o` may spell too, where neither is open;
  // `same(t, u)` says whether two tokens may spell one text.
  template <class Same = std::equal_to<>>
  std::optional<std::vector<std::uint16_t>> common(const OperatorWords& o,
                                                   Same same = {}) const {
    if (_open || o._open) return std::nullopt;
    for (const auto& a : _words)
      for (const auto& b : o._words)
        if (std::ranges::equal(a, b, same)) return a;
    return std::nullopt;
  }

  template <class Same = std::equal_to<>>
  bool overlaps(const OperatorWords& o, Same same = {}) const {
    if (!_open && !o._open) return common(o, same).has_value();
    const std::vector<std::uint16_t> theirs = o.opening();
    return std::ranges::any_of(opening(), [&](std::uint16_t t) {
      return std::ranges::any_of(theirs,
                                 [&](std::uint16_t u) { return same(t, u); });
    });
  }

 private:
  // `_words` and `_opening` stay sorted and each entry once.
  std::vector<std::uint16_t> opening() const {
    std::vector<std::uint16_t> out = _opening;
    for (const auto& w : _words)
      if (!w.empty()) out.push_back(w.front());
    return out;
  }
  void normalize() {
    std::ranges::sort(_words);
    _words.erase(std::ranges::unique(_words).begin(), _words.end());
    if (_words.size() > kLimit) {
      _open = true;
      _opening = opening();
      _words.clear();
    }
    std::ranges::sort(_opening);
    _opening.erase(std::ranges::unique(_opening).begin(), _opening.end());
  }

  std::vector<std::vector<std::uint16_t>> _words{};
  std::vector<std::uint16_t> _opening{};
  bool _open = false;
};

}  // namespace cursiv::engine
