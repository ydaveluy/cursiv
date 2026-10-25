#include "cursiv/engines/shared/gen/SelfBoundedArm.hpp"

#include <algorithm>
#include <cstdint>

#include "cursiv/engines/shared/gen/WarningFound.hpp"

namespace cursiv {
namespace {

enum class Edge { front, back };

class Arms {
 public:
  Arms(const GrammarImage& image, std::uint32_t rule)
      : _image(image), _rule(rule) {}

  // Whether the first (or last) reader of an expression is a call to the
  // rule; the conditions around it read nothing and are passed over.
  bool calls_at_edge(Edge edge, ImageId id) const {
    const ImageNode& n = _image.nodes[id];
    if (over_operand(n.kind)) return calls_at_edge(edge, _image.kids[n.first]);
    switch (n.kind) {
      case ImageKind::ref:
        return n.a == _rule;
      case ImageKind::seq:
        return readers_call_at_edge(edge, readers_of(n));
      default:
        return false;
    }
  }

  bool self_bounded(ImageId id) const {
    const ImageNode& n = _image.nodes[id];
    if (over_operand(n.kind)) return self_bounded(_image.kids[n.first]);
    switch (n.kind) {
      case ImageKind::choice:
        for (std::uint32_t i = 0; i < n.count; ++i)
          if (self_bounded(_image.kids[n.first + i])) return true;
        return false;
      case ImageKind::seq: {
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
  static bool condition(ImageKind k) {
    return k == ImageKind::andOf || k == ImageKind::notOf ||
           k == ImageKind::gapHas || k == ImageKind::gapIs ||
           k == ImageKind::eof;
  }

  bool readers_call_at_edge(Edge edge,
                            const std::vector<ImageId>& readers) const {
    if (readers.empty()) return false;
    return calls_at_edge(
        edge, edge == Edge::front ? readers.front() : readers.back());
  }

  // What a sequence reads: its elements, but for the conditions around them.
  std::vector<ImageId> readers_of(const ImageNode& seq) const {
    std::vector<ImageId> out;
    for (std::uint32_t i = 0; i < seq.count; ++i) {
      const ImageId k = _image.kids[seq.first + i];
      if (!condition(_image.nodes[k].kind)) out.push_back(k);
    }
    return out;
  }

  const GrammarImage& _image;
  std::uint32_t _rule;
};

}  // namespace

std::vector<SelfBoundedArm> self_bounded_arms(GrammarImage& image) {
  WarningFound w(image);
  std::vector<SelfBoundedArm> found;
  for (const std::uint32_t rule : w.rules()) {
    const std::string name = w.name(rule);
    const Arms arms(image, rule);
    const ImageId body = w.body(rule);
    const ImageNode n = image.nodes[body];
    const std::uint32_t count = n.kind == ImageKind::choice ? n.count : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
      const ImageId arm =
          n.kind == ImageKind::choice ? image.kids[n.first + i] : body;
      if (!arms.self_bounded(arm)) continue;
      SelfBoundedArm a{name, w.spell(arm)};
      if (std::ranges::find(found, a) == found.end())
        found.push_back(std::move(a));
    }
  }
  return found;
}

std::string self_bounded_arm_text(const SelfBoundedArm& a) {
  return "rule `" + a.rule + "`: the arm `" + a.arm +
         "` begins and ends with `" + a.rule +
         "`, which says nothing of how its operands nest -- declare the "
         "operators with infix<>, or give each level its own rule (`" +
         a.rule + " := " + a.rule + " + T | T`)";
}

}  // namespace cursiv
