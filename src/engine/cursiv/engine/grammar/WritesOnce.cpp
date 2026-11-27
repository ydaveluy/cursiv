#include "cursiv/engine/grammar/WritesOnce.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "cursiv/engine/grammar/Refused.hpp"
#include "cursiv/utils/Descent.hpp"

namespace cursiv::engine::gen {

using detail::Descent;

namespace {

using Members = std::uint64_t;

class Check {
 public:
  Check(GrammarImage& image, const LowerDesc& desc) : _image(image) {
    for (const RowDesc* row : desc.rows)
      if (row->member && _number.try_emplace(row->member, _names.size()).second)
        _names.push_back(row->member->name);
    _single.resize(_names.size());
    for (std::size_t i = 0; i < std::min<std::size_t>(64, _single.size()); ++i)
      _single[i] = Members{1} << i;
    for (std::size_t width = 64; width < _single.size(); width *= 2) {
      for (std::size_t i = 0; i < width; ++i) {
        const auto one = _single[i];
        _single[i] = branch_id({one, 0});
        if (i + width < _single.size()) _single[i + width] = branch_id({0, one});
      }
      ++_depth;
    }
    schedule(_image.entry_rule(_image.rule(*desc.entry)));
  }

  // A rule waits under the rules it meets first, and is read again when one it
  // calls is found to write more into the node it hands over.
  std::size_t run() {
    while (!_todo.empty()) {
      _rule = _todo.back();
      const std::size_t waiting = _todo.size();
      const ImageStageDesc& stage = _image.stage(_rule);
      Members wrote = 0;
      switch (stage.kind) {
        case ImageRuleKind::node:
          wrote = detail::descend(
              written(_image.node(*stage.body, _image.folds(_rule)))).members;
          break;
        case ImageRuleKind::infix: {
          // Its node is its primary's, or one of its levels' with their
          // members written.
          const std::uint32_t primary = _image.rule(*stage.primary);
          wrote = made(primary);
          Members operands = 0;
          unsigned twice = none;
          for (const void* row : {stage.leftRow, stage.rightRow})
            if (const auto m = member(row); m != none && !merge(operands, _single[m])) twice = m;
          for (std::uint32_t l = 0; l < stage.levelCount; ++l) {
            // A binary level writes both operands; a prefix or postfix level
            // builds its own node: its operand member, then its operator's.
            const Association group = stage.levels[l].group;
            const bool unary = group == Association::Prefix || group == Association::Postfix;
            const unsigned operand = member(stage.levels[l].operandRow);
            if (!unary && twice != none) refuse(twice, "twice");
            if (!unary) merge(wrote, operands);
            if (operand != none) merge(wrote, _single[operand]);
            for (std::uint32_t o = 0; o < stage.levels[l].count; ++o)
              if (const auto m = member(stage.levels[l].ops[o].row); m != none) {
                if (unary ? m == operand : overlap(operands, _single[m]) != none) refuse(m, "twice");
                merge(wrote, _single[m]);
              }
          }
          break;
        }
        default:
          break;
      }
      slot(_seen, _rule) = true;
      if (_todo.size() != waiting) continue;
      _todo.pop_back();
      _queued[_rule] = false;
      Members& known = slot(_made, _rule);
      if (!merge(known, wrote)) continue;
      for (const std::uint32_t user : slot(_users, _rule)) schedule(user);
    }
    return _branches.size();
  }

 private:
  // What an expression wrote into the node of the place it stands in;
  // `renewed` where that node is a new one from there on, `members` then
  // being the new one's and `first` what the node before it received.
  struct Wrote {
    Members members = 0;
    bool renewed = false;
    Members first = 0;
  };

  template <class T>
  static T& slot(std::vector<T>& of, std::uint32_t index) {
    if (index >= of.size()) of.resize(index + 1);
    return of[index];
  }

  void schedule(std::uint32_t rule) {
    if (slot(_queued, rule)) return;
    slot(_queued, rule) = true;
    _todo.push_back(rule);
  }

  // What the node a rule hands over has written, as known so far; the rule
  // being read is read again when more is known.
  Members made(std::uint32_t rule) {
    rule = _image.entry_rule(rule);
    const ImageRuleKind kind = _image.stage(rule).kind;
    if (kind != ImageRuleKind::node && kind != ImageRuleKind::infix) return {};
    if (!slot(_seen, _rule)) {
      auto& users = slot(_users, rule);
      if (users.empty() || users.back() != _rule) users.push_back(_rule);
      if (!slot(_seen, rule)) schedule(rule);
    }
    return slot(_made, rule);
  }

  static constexpr unsigned none = ~0u;
  unsigned member(const void* row) const {
    const auto* r = static_cast<const RowDesc*>(row);
    return r && r->member ? _number.at(r->member) : none;
  }

  [[noreturn]] void refuse(unsigned m, std::string_view how) const {
    throw Refused("rule " + std::string(_image.rules[_rule]->name) + " writes the member '" +
                  std::string(_names[m]) + "' " + std::string(how) +
                  ": a member that holds one value is written once into a node");
  }

  // Leaves hold 64 properties. A union shares every unchanged branch with its
  // operands. At most 26 levels cover the entire range of property numbers.
  Members branch_id(std::array<Members, 2> branch) {
    _branches.push_back(branch);
    return _branches.size();
  }

  static constexpr unsigned kMaxDepth = std::numeric_limits<unsigned>::digits - 6;

  bool merge(Members& set, Members other) {
    struct Frame {
      Members a, b, low;
      bool high;
    };
    std::array<Frame, kMaxDepth> path;
    Members a = set, b = other;
    unsigned depth = _depth;
    for (;;) {
      if (depth && a && b && a != b) {
        path[--depth] = {a, b, 0, false};
        a = _branches[a - 1][0];
        b = _branches[b - 1][0];
        continue;
      }
      Members joined = a | b;
      while (depth < _depth) {
        auto& frame = path[depth];
        const auto left = _branches[frame.a - 1], right = _branches[frame.b - 1];
        if (!frame.high) {
          frame.low = joined;
          frame.high = true;
          a = left[1];
          b = right[1];
          break;
        }
        const std::array branch{frame.low, joined};
        joined = branch == left ? frame.a : branch == right ? frame.b : branch_id(branch);
        ++depth;
      }
      if (depth == _depth) {
        if (joined == set) return false;
        set = joined;
        return true;
      }
    }
  }

  unsigned overlap(Members a, Members b) const {
    struct Pair { Members a, b; unsigned depth, base; };
    std::array<Pair, kMaxDepth + 1> work;
    unsigned count = 0;
    work[count++] = {a, b, _depth, 0};
    while (count) {
      const auto [x, y, depth, base] = work[--count];
      if (!x || !y) continue;
      if (!depth) {
        if (const auto common = x & y) return base + std::countr_zero(common);
        continue;
      }
      const auto& left = _branches[x - 1];
      const auto& right = _branches[y - 1];
      work[count++] = {left[1], right[1], depth - 1, base + (32u << depth)};
      work[count++] = {left[0], right[0], depth - 1, base};
    }
    return none;
  }

  void check_overlap(Members a, Members b, std::string_view how) const {
    if (const auto m = overlap(a, b); m != none) refuse(m, how);
  }

  Descent<Wrote> written(ImageId id) {
    const ImageNode n = _image.nodes[id];
    Wrote out;
    switch (n.kind) {
      case ImageKind::notOf:
      case ImageKind::andOf:
        // A condition reads without writing.
        co_return out;
      case ImageKind::ref:
        out.members = made(n.a);
        out.first = out.members;
        co_return out;
      case ImageKind::choice:
        // A new node from there on where every alternative opens one.
        out.renewed = n.count != 0;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          Wrote alternative = co_await written(_image.children[n.first + i]);
          merge(out.members, alternative.members);
          merge(out.first, alternative.first);
          out.renewed = out.renewed && alternative.renewed;
        }
        co_return out;
      case ImageKind::unordered:
        // Any two items can be neighbours in either order. Any item can
        // write first or last, and a required renewal separates the nodes.
        for (std::uint32_t i = 0; i < n.count; ++i) {
          Wrote part = co_await written(_image.children[n.first + i]);
          check_overlap(out.members, part.first, "twice");
          check_overlap(out.first, part.members, "twice");
          merge(out.members, part.members);
          merge(out.first, part.first);
          out.renewed |= part.renewed;
        }
        co_return out;
      case ImageKind::repeat: {
        out = co_await written(_image.children[n.first]);
        if (n.n > 1) check_overlap(out.members, out.first, "at every turn of a repetition");
        out.renewed &= n.m != 0;
        co_return out;
      }
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag: {
        // What the value is made of writes into its own node.
        Wrote inside = co_await written(_image.children[n.first]);
        if (n.kind == ImageKind::flag) out = std::move(inside);
        if (const auto m = member(n.row); m != none && n.kind != ImageKind::append) {
          if (!merge(out.members, _single[m])) refuse(m, "twice");
          if (!out.renewed) merge(out.first, _single[m]);
        }
        co_return out;
      }
      case ImageKind::nest: {
        // The node so far goes into a member of a new one, which is written
        // from there on, until a nest in what it reads makes another.
        out = co_await written(_image.children[n.first]);
        if (const auto m = member(n.row); m != none) {
          if (overlap(out.first, _single[m]) != none) refuse(m, "twice");
          if (!out.renewed) merge(out.members, _single[m]);
        }
        out.renewed = true;
        out.first = {};
        co_return out;
      }
      default:
        break;
    }
    // Anything else reads its parts one after the other.
    for (std::uint32_t i = 0; i < n.count; ++i) {
      Wrote part = co_await written(_image.children[n.first + i]);
      check_overlap(part.first, out.members, "twice");
      if (!out.renewed) merge(out.first, part.first);
      if (part.renewed) {
        out.members = part.members;
        out.renewed = true;
        continue;
      }
      merge(out.members, part.members);
    }
    co_return out;
  }

  GrammarImage& _image;
  std::unordered_map<const MemberDesc*, unsigned> _number;
  std::vector<std::string_view> _names;
  unsigned _depth = 0;
  std::vector<std::array<Members, 2>> _branches;
  std::vector<Members> _single;
  // Per rule: what the node it hands over has written, the rules that read
  // that, whether it was met, and whether it waits to be read.
  std::vector<Members> _made;
  std::vector<std::vector<std::uint32_t>> _users;
  std::vector<char> _seen;
  std::vector<char> _queued;
  std::vector<std::uint32_t> _todo;
  std::uint32_t _rule = 0;
};

}  // namespace

std::size_t check_writes_once(GrammarImage& image, const LowerDesc& desc) {
  return Check(image, desc).run();
}

}  // namespace cursiv::engine::gen
