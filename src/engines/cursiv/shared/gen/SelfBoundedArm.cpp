#include "cursiv/shared/gen/SelfBoundedArm.hpp"

#include <algorithm>
#include <cstdint>

#include "cursiv/shared/gen/WarningFound.hpp"

namespace cursiv {
namespace {

enum class Edge { front, back };

class Arms {
 public:
  Arms(const detail::GrammarImage& image, std::uint32_t rule)
      : _image(image), _rule(rule) {}

  // Whether the first (or last) reader of an expression is a call to the
  // rule; the conditions around it read nothing and are passed over.
  bool calls_at_edge(Edge edge, detail::ImageId id) const {
    const detail::ImageNode& n = _image.nodes[id];
    if (over_operand(n.kind)) return calls_at_edge(edge, _image.kids[n.first]);
    switch (n.kind) {
      case detail::ImageKind::ref:
        return n.a == _rule;
      case detail::ImageKind::seq:
        return readers_call_at_edge(edge, readers_of(n));
      default:
        return false;
    }
  }

  bool self_bounded(detail::ImageId id) const {
    const detail::ImageNode& n = _image.nodes[id];
    if (over_operand(n.kind)) return self_bounded(_image.kids[n.first]);
    switch (n.kind) {
      case detail::ImageKind::choice:
        for (std::uint32_t i = 0; i < n.count; ++i)
          if (self_bounded(_image.kids[n.first + i])) return true;
        return false;
      case detail::ImageKind::seq: {
        const auto readers = readers_of(n);
        return readers.size() >= 2 &&
               readers_call_at_edge(Edge::front, readers) &&
               readers_call_at_edge(Edge::back, readers);
      }
      default:
        return false;
    }
  }

 private:
  bool readers_call_at_edge(Edge edge,
                            const std::vector<detail::ImageId>& readers) const {
    if (readers.empty()) return false;
    return calls_at_edge(
        edge, edge == Edge::front ? readers.front() : readers.back());
  }

  // What a sequence reads: its elements, but for the conditions around them.
  std::vector<detail::ImageId> readers_of(const detail::ImageNode& seq) const {
    std::vector<detail::ImageId> out;
    for (std::uint32_t i = 0; i < seq.count; ++i) {
      const detail::ImageId k = _image.kids[seq.first + i];
      if (!is_condition_node(_image, k)) out.push_back(k);
    }
    return out;
  }

  const detail::GrammarImage& _image;
  std::uint32_t _rule;
};

}  // namespace

std::vector<SelfBoundedArm> self_bounded_arms(detail::GrammarImage& image) {
  WarningFound w(image);
  std::vector<SelfBoundedArm> found;
  for (const std::uint32_t rule : w.rules()) {
    const std::string name = w.name(rule);
    const Arms arms(image, rule);
    const detail::ImageId body = w.body(rule);
    const detail::ImageNode n = image.nodes[body];
    const std::uint32_t count =
        n.kind == detail::ImageKind::choice ? n.count : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
      const detail::ImageId arm =
          n.kind == detail::ImageKind::choice ? image.kids[n.first + i] : body;
      if (!arms.self_bounded(arm)) continue;
      SelfBoundedArm a{name, w.spell(arm)};
      if (std::ranges::find(found, a) == found.end())
        found.push_back(std::move(a));
    }
  }
  return found;
}

std::string self_bounded_arm_text(const SelfBoundedArm& a) {
  return "rule `" + a.rule + "`: the alternative `" + a.arm +
         "` begins and ends with `" + a.rule +
         "`, so it does not say how its operands group -- declare the "
         "operators with `infix<>`, or give each level its own rule (`" +
         a.rule + " := " + a.rule + " + T | T`)";
}

}  // namespace cursiv
