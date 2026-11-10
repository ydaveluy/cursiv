#include "cursiv/shared/gen/SelfBoundedArm.hpp"

#include <algorithm>
#include <cstdint>

#include "cursiv/shared/gen/WarningFound.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {
namespace {

enum class Edge { front, back };

class Arms {
 public:
  Arms(const GrammarImage& image, std::uint32_t rule)
      : _image(image), _rule(rule) {}

  // Whether the first (or last) reader of an expression is a call to the
  // rule; the conditions around it read nothing and are passed over.
  Descent<bool> calls_at_edge(Edge edge, ImageId id) const {
    const ImageNode& n = _image.nodes[id];
    if (over_operand(n.kind))
      co_return co_await calls_at_edge(edge, _image.children[n.first]);
    switch (n.kind) {
      case ImageKind::ref:
        co_return n.a == _rule;
      case ImageKind::seq:
        co_return co_await readers_call_at_edge(edge, readers_of(n));
      default:
        co_return false;
    }
  }

  Descent<bool> self_bounded(ImageId id) const {
    const ImageNode& n = _image.nodes[id];
    if (over_operand(n.kind))
      co_return co_await self_bounded(_image.children[n.first]);
    switch (n.kind) {
      case ImageKind::choice:
        for (std::uint32_t i = 0; i < n.count; ++i)
          if (co_await self_bounded(_image.children[n.first + i]))
            co_return true;
        co_return false;
      case ImageKind::seq: {
        const auto readers = readers_of(n);
        if (readers.size() < 2) co_return false;
        if (!co_await readers_call_at_edge(Edge::front, readers))
          co_return false;
        co_return co_await readers_call_at_edge(Edge::back, readers);
      }
      default:
        co_return false;
    }
  }

 private:
  Descent<bool> readers_call_at_edge(
      Edge edge, const std::vector<ImageId>& readers) const {
    if (readers.empty()) co_return false;
    co_return co_await calls_at_edge(
        edge, edge == Edge::front ? readers.front() : readers.back());
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
          n.kind == ImageKind::choice ? image.children[n.first + i] : body;
      if (!descend(arms.self_bounded(arm))) continue;
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
