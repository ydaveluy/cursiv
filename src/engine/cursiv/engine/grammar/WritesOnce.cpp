#include "cursiv/engine/grammar/WritesOnce.hpp"

#include <bit>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cursiv/engine/grammar/Refused.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/Descent.hpp"

namespace cursiv::engine::gen {

using detail::Descent;

namespace {

// Only occupied words are stored. A set contained in one word needs no allocation.
struct Members {
  std::uint64_t bits = 0;
  unsigned first = 0;
  using Blocks = std::map<unsigned, std::uint64_t>;
  std::unique_ptr<Blocks> rest;

  Members clone() const {
    Members copy{bits, first, nullptr};
    if (rest) copy.rest = std::make_unique<Blocks>(*rest);
    return copy;
  }

  std::uint64_t get(unsigned block) const {
    if (block == first) return bits;
    if (!rest) return 0;
    const auto it = rest->find(block);
    return it == rest->end() ? 0 : it->second;
  }
  bool add(unsigned block, std::uint64_t mask) {
    if (!bits) first = block;
    if (block != first && !rest) rest = std::make_unique<Blocks>();
    auto& word = block == first ? bits : (*rest)[block];
    const bool grew = (mask & ~word) != 0;
    word |= mask;
    return grew;
  }
  bool insert(unsigned id) { return add(id / 64, std::uint64_t{1} << (id % 64)); }
  bool contains(unsigned id) const { return (get(id / 64) >> (id % 64)) & 1; }

  void each(auto&& visit) const {
    if (bits) visit(first, bits);
    if (rest)
      for (const auto& [block, mask] : *rest) visit(block, mask);
  }
  bool merge(const Members& other) {
    bool grew = false;
    other.each([&](unsigned block, std::uint64_t mask) { grew |= add(block, mask); });
    return grew;
  }
};

class Check {
 public:
  Check(GrammarImage& image, const LowerDesc& desc) : _image(image) {
    for (const RowDesc* row : desc.rows)
      if (row->member && _number.try_emplace(row->member, _names.size()).second)
        _names.push_back(row->member->name);
    schedule(_image.entry_rule(_image.rule(*desc.entry)));
  }

  // A rule is read again when a rule it calls is found to write more into the
  // node it hands over.
  void run() {
    while (!_todo.empty()) {
      _rule = _todo.back();
      _todo.pop_back();
      const ImageStageDesc& stage = _image.stage(_rule);
      Members wrote;
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
          Members operands;
          for (const void* row : {stage.leftRow, stage.rightRow})
            if (const auto m = member(row); m != none && !operands.insert(m)) refuse(m, "twice");
          wrote.merge(operands);
          for (std::uint32_t l = 0; l < stage.levelCount; ++l)
            for (std::uint32_t o = 0; o < stage.levels[l].count; ++o)
              if (const auto m = member(stage.levels[l].ops[o].row); m != none) {
                if (operands.contains(m)) refuse(m, "twice");
                wrote.insert(m);
              }
          break;
        }
        default:
          break;
      }
      slot(_seen, _rule) = true;
      _queued[_rule] = false;
      Members& known = slot(_made, _rule);
      if (!known.merge(wrote)) continue;
      for (const std::uint32_t user : slot(_users, _rule)) schedule(user);
    }
  }

 private:
  // What an expression wrote into the node of the place it stands in;
  // `renewed` where that node is a new one from there on, `members` then
  // being the new one's and `first` what the node before it received.
  struct Wrote {
    Members members;
    bool renewed = false;
    Members first;
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
    return slot(_made, rule).clone();
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

  void check_overlap(const Members& a, const Members& b, std::string_view how) const {
    if (!a.bits || !b.bits) return;
    const bool smaller = !a.rest || (b.rest && a.rest->size() < b.rest->size());
    const auto& own = smaller ? a : b;
    const auto& other = smaller ? b : a;
    unsigned first = none;
    own.each([&](unsigned block, std::uint64_t mask) {
      if (const auto shared = mask & other.get(block))
        first = std::min(first, block * 64 + static_cast<unsigned>(std::countr_zero(shared)));
    });
    if (first != none) refuse(first, how);
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
        out.first = out.members.clone();
        co_return out;
      case ImageKind::choice:
        // A new node from there on where every alternative opens one.
        out.renewed = n.count != 0;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          Wrote alternative = co_await written(_image.children[n.first + i]);
          out.members.merge(alternative.members);
          out.first.merge(alternative.first);
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
          out.members.merge(part.members);
          out.first.merge(part.first);
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
          if (!out.members.insert(m)) refuse(m, "twice");
          if (!out.renewed) out.first.insert(m);
        }
        co_return out;
      }
      case ImageKind::nest: {
        // The node so far goes into a member of a new one, which is written
        // from there on, until a nest in what it reads makes another.
        out = co_await written(_image.children[n.first]);
        if (const auto m = member(n.row); m != none) {
          if (out.first.contains(m)) refuse(m, "twice");
          if (!out.renewed) out.members.insert(m);
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
      if (!out.renewed) out.first.merge(part.first);
      if (part.renewed) {
        out.members = std::move(part.members);
        out.renewed = true;
        continue;
      }
      out.members.merge(part.members);
    }
    co_return out;
  }

  GrammarImage& _image;
  std::unordered_map<const MemberDesc*, unsigned> _number;
  std::vector<std::string_view> _names;
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

void check_writes_once(GrammarImage& image, const LowerDesc& desc) {
  Check(image, desc).run();
}

}  // namespace cursiv::engine::gen
