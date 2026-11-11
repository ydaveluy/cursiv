#include "cursiv/shared/gen/SelfBoundedArm.hpp"

#include <algorithm>
#include <cstdint>

#include "cursiv/shared/gen/WarningFound.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {
namespace {

class Arms {
 public:
  Arms(const GrammarImage& image, std::uint32_t rule)
      : _image(image), _rule(rule) {}

  // Whether an alternative of the expression is a sequence of two readers or
  // more that begins and ends with a call to the rule.
  bool self_bounded(ImageId id) const {
    for (std::vector todo{id}; !todo.empty();) {
      const ImageNode& n = _image.nodes[todo.back()];
      todo.pop_back();
      if (over_operand(n.kind)) {
        todo.push_back(_image.children[n.first]);
      } else if (n.kind == ImageKind::choice) {
        for (std::uint32_t i = 0; i < n.count; ++i)
          todo.push_back(_image.children[n.first + i]);
      } else if (n.kind == ImageKind::seq) {
        const auto readers = readers_of(n);
        if (readers.size() >= 2 && calls_at_edge(true, readers.front()) &&
            calls_at_edge(false, readers.back()))
          return true;
      }
    }
    return false;
  }

 private:
  // Whether the first (or last) reader of an expression is a call to the
  // rule; the conditions around it read nothing and are passed over.
  bool calls_at_edge(bool front, ImageId id) const {
    for (;;) {
      const ImageNode& n = _image.nodes[id];
      if (over_operand(n.kind)) {
        id = _image.children[n.first];
      } else if (n.kind == ImageKind::seq) {
        const auto readers = readers_of(n);
        if (readers.empty()) return false;
        id = front ? readers.front() : readers.back();
      } else {
        return n.kind == ImageKind::ref && n.a == _rule;
      }
    }
  }

  // What a sequence reads: its elements, but for the conditions around them.
  std::vector<ImageId> readers_of(const ImageNode& seq) const {
    std::vector<ImageId> out;
    for (std::uint32_t i = 0; i < seq.count; ++i) {
      const ImageId k = _image.children[seq.first + i];
      if (!is_condition_node(_image, k)) out.push_back(k);
    }
    return out;
  }

  const GrammarImage& _image;
  std::uint32_t _rule;
};

}  // namespace

std::vector<SelfBoundedArm> self_bounded_arms(
    GrammarImage& image, std::span<const ImageRuleDesc* const> reached) {
  WarningFound w(image, reached);
  std::vector<SelfBoundedArm> found;
  for (const std::uint32_t rule : w.rules()) {
    const std::string name = w.name(rule);
    const Arms arms(image, rule);
    const ImageId body = w.body(rule);
    const ImageNode n = image.nodes[body];
    const std::uint32_t count = n.kind == ImageKind::choice ? n.count : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
      const ImageId arm =
          n.kind == ImageKind::choice ? image.children[n.first + i] : body;
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

}  // namespace cursiv::engine
