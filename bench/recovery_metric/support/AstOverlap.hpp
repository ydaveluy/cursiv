// How much of the twin's tree a repaired tree still holds. A twin node a
// fault touches is left out of the count; every other one is kept when the
// descent reaches it through nodes of the same class and its own values read
// the same. A list is aligned, never zipped: equal subtrees anchor it and the
// stretches between align on class and values, so a lost element costs its
// own subtree and nothing after it. Values are read through the fuzz lane's
// directive reader; without one, a node is weighed on its class alone.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/fuzz/GrammarActions.hpp"

namespace cursiv {

struct AstOverlap {
  const AstReflection& reflection;
  const fuzzer::GrammarActions* actions = nullptr;

  struct Result {
    std::uint64_t total = 0, kept = 0;
    // Stretches too long to align, paired in order instead: counted, since
    // the pairing then decides what was kept.
    std::uint64_t unaligned = 0;
  };

  [[nodiscard]] Result run(const AstNode& want, const AstNode* got,
                           function_ref<bool(const AstNode&)> touched) const {
    Walk w{*this, touched, {}, {}, {}};
    Result r;
    r.total = w.twin(want).spared;
    if (got != nullptr) w.match(want, *got, r);
    return r;
  }

  // The two trees are one: same classes, same values, same shape.
  [[nodiscard]] bool same(const AstNode& want, const AstNode& got) const {
    const auto none = [](const AstNode&) { return false; };
    Walk w{*this, none, {}, {}, {}};
    return w.seal(want, true).whole == w.seal(got, false).whole;
  }

 private:
  // Past this many cells a stretch is paired in order instead of aligned:
  // the table is the alignment's memory.
  static constexpr std::size_t kCells = std::size_t{1} << 22;

  struct Kid {
    std::string property;
    std::uint32_t index;
    const AstNode* node;
  };
  struct Seal {
    std::uint64_t whole = 0, own = 0, spared = 0;
    bool touched = false;
  };

  static std::uint64_t mix(std::uint64_t h, std::string_view s) {
    for (const char ch : s) {
      h ^= static_cast<unsigned char>(ch);
      h *= 1099511628211ull;
    }
    return h * 1099511628211ull + 0x9e3779b97f4a7c15ull;
  }
  static std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 1099511628211ull;
  }

  // Pairs of indices a longest common subsequence of `eq` takes, in order.
  template <class Eq>
  static std::vector<std::pair<std::size_t, std::size_t>> lcs(
      std::size_t n, std::size_t m, Eq&& eq, Result& r) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    if (n == 0 || m == 0) return out;
    if (n * m > kCells) {
      ++r.unaligned;
      for (std::size_t k = 0; k < n && k < m; ++k)
        if (eq(k, k)) out.emplace_back(k, k);
      return out;
    }
    std::vector<std::uint32_t> t((n + 1) * (m + 1));
    const auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& {
      return t[i * (m + 1) + j];
    };
    for (std::size_t i = n; i-- > 0;)
      for (std::size_t j = m; j-- > 0;)
        at(i, j) = eq(i, j) ? at(i + 1, j + 1) + 1
                            : std::max(at(i + 1, j), at(i, j + 1));
    for (std::size_t i = 0, j = 0; i < n && j < m;) {
      if (eq(i, j) && at(i, j) == at(i + 1, j + 1) + 1) {
        out.emplace_back(i, j);
        ++i;
        ++j;
      } else if (at(i + 1, j) >= at(i, j + 1)) {
        ++i;
      } else {
        ++j;
      }
    }
    return out;
  }

  struct Walk {
    const AstOverlap& self;
    function_ref<bool(const AstNode&)> touched;
    std::unordered_map<const AstNode*, Seal> seals;
    std::unordered_map<const AstNode*, Seal> gotSeals;
    std::unordered_map<const AstNode*, std::string> owns;

    const std::string& own(const AstNode& n) {
      const auto it = owns.find(&n);
      if (it != owns.end()) return it->second;
      std::string s;
      if (self.actions != nullptr)
        self.actions->visitValues(
            n, [&](std::string_view p, std::uint32_t i, fuzzer::ValueKind k,
                   std::string_view text) {
              if (k == fuzzer::ValueKind::unreadable) return;
              s.append(p);
              s += '#';
              s += std::to_string(i);
              s += '=';
              s.append(text);
              s += '\x1f';
            });
      return owns.emplace(&n, std::move(s)).first->second;
    }

    std::vector<Kid> kids(const AstNode& n) const {
      std::vector<Kid> out;
      self.reflection.visitMembers(
          n, [&](std::string_view p, std::uint32_t i, AstNode& c) {
            out.push_back(Kid{std::string(p), i, &c});
          });
      return out;
    }

    Seal seal(const AstNode& n, bool twinSide) {
      auto& memo = twinSide ? seals : gotSeals;
      const auto it = memo.find(&n);
      if (it != memo.end()) return it->second;
      Seal s;
      s.touched = twinSide && touched(n);
      s.own = mix(mix(1469598103934665603ull,
                      reinterpret_cast<std::uintptr_t>(n.type)),
                  own(n));
      s.whole = s.own;
      s.spared = s.touched ? 0 : 1;
      for (const Kid& k : kids(n)) {
        const Seal c = seal(*k.node, twinSide);
        s.whole = mix(mix(mix(s.whole, k.property), std::uint64_t{k.index}),
                      c.whole);
        s.spared += c.spared;
      }
      memo[&n] = s;
      return s;
    }
    Seal twin(const AstNode& n) { return seal(n, true); }

    void match(const AstNode& a, const AstNode& b, Result& r) {
      if (a.type != b.type) return;
      const Seal sa = twin(a);
      if (!sa.touched && sa.own == seal(b, false).own) ++r.kept;
      const std::vector<Kid> ka = kids(a), kb = kids(b);
      std::size_t i = 0;
      while (i < ka.size()) {
        std::size_t e = i;
        while (e < ka.size() && ka[e].property == ka[i].property) ++e;
        std::vector<const AstNode*> la, lb;
        for (std::size_t k = i; k < e; ++k) la.push_back(ka[k].node);
        for (const Kid& k : kb)
          if (k.property == ka[i].property) lb.push_back(k.node);
        align(la, lb, r);
        i = e;
      }
    }

    void align(const std::vector<const AstNode*>& la,
               const std::vector<const AstNode*>& lb, Result& r) {
      const auto whole = [&](std::size_t i, std::size_t j) {
        return twin(*la[i]).whole == seal(*lb[j], false).whole;
      };
      std::size_t head = 0, tail = 0;
      while (head < la.size() && head < lb.size() && whole(head, head)) {
        r.kept += twin(*la[head]).spared;
        ++head;
      }
      while (tail + head < la.size() && tail + head < lb.size() &&
             whole(la.size() - 1 - tail, lb.size() - 1 - tail)) {
        r.kept += twin(*la[la.size() - 1 - tail]).spared;
        ++tail;
      }
      const std::size_t na = la.size() - head - tail,
                        nb = lb.size() - head - tail;
      const auto anchors = lcs(
          na, nb,
          [&](std::size_t i, std::size_t j) { return whole(head + i, head + j); },
          r);
      std::size_t pa = head, pb = head;
      const auto stretch = [&](std::size_t ea, std::size_t eb) {
        const auto like = [&](std::size_t i, std::size_t j) {
          const AstNode& x = *la[pa + i];
          const AstNode& y = *lb[pb + j];
          if (x.type != y.type) return false;
          const Seal sx = twin(x);
          return sx.touched || sx.own == seal(y, false).own;
        };
        // A lone element left on each side between two pairs is the same
        // element changed in place: its class is enough to descend.
        std::size_t ga = 0, gb = 0;
        const auto lone = [&](std::size_t i, std::size_t j) {
          if (i - ga == 1 && j - gb == 1 && la[pa + ga]->type == lb[pb + gb]->type)
            match(*la[pa + ga], *lb[pb + gb], r);
        };
        for (const auto& [i, j] : lcs(ea - pa, eb - pb, like, r)) {
          lone(i, j);
          match(*la[pa + i], *lb[pb + j], r);
          ga = i + 1;
          gb = j + 1;
        }
        lone(ea - pa, eb - pb);
      };
      for (const auto& [i, j] : anchors) {
        stretch(head + i, head + j);
        r.kept += twin(*la[head + i]).spared;
        pa = head + i + 1;
        pb = head + j + 1;
      }
      stretch(head + na, head + nb);
    }
  };
};

}  // namespace cursiv
