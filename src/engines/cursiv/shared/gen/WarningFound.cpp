#include "cursiv/shared/gen/WarningFound.hpp"

#include <cstddef>
#include <stdexcept>
#include <unordered_set>

#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

namespace {

struct Item {
  std::uint32_t rule = 0;
  bool condition = false;
};

// The rules a grammar reaches from `entry`, `ignored` and `hidden`, in order.
// A rule a condition names is reached, but read there as text.
class Reach {
 public:
  explicit Reach(GrammarImage& image) : _image(image) {}

  std::vector<std::uint32_t> run() {
    push(_image.entry, false);
    for (std::uint32_t i = 0; i < _image.ignoreCount; ++i)
      push(_image.ruleLists[_image.ignoreAt + i], false);
    for (std::uint32_t i = 0; i < _image.hiddenCount; ++i)
      push(_image.ruleLists[_image.hiddenAt + i], false);
    for (std::size_t i = 0; i < _queue.size(); ++i) {
      const Item item = _queue[i];
      _out.clear();
      collect(item.rule);
      for (const Item& found : _out)
        push(found.rule, item.condition || found.condition);
    }
    std::vector<std::uint32_t> visible;
    for (const Item& item : _queue)
      if (!item.condition) visible.push_back(item.rule);
    return visible;
  }

 private:
  void push(std::uint32_t rule, bool condition) {
    const std::uint64_t key = (std::uint64_t{rule} << 1) | (condition ? 1 : 0);
    if (_seen.insert(key).second) _queue.push_back({rule, condition});
  }

  void list(std::uint32_t at, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i)
      _out.push_back({_image.ruleLists[at + i], false});
  }

  void collect(std::uint32_t rule) {
    const ImageFace f = _image.ready(rule).face[1];
    if (f.kind == ImageRuleKind::infix) {
      _out.push_back({f.primary, false});
      for (std::uint32_t i = 0; i < f.levelCount; ++i)
        descend(plain(_image.levels[f.levelsAt + i].op));
    } else if (f.kind != ImageRuleKind::grammar) {
      descend(plain(f.body));
    }
  }

  // The rules an expression names where it is read.
  Descent<void> plain(ImageId id) {
    switch (const ImageNode n = _image.nodes[id]; n.kind) {
      case ImageKind::ref:
        _out.push_back({n.a, false});
        co_return;
      case ImageKind::embed:
        if (_image.nodes[_image.children[n.first]].kind == ImageKind::seq &&
            _image.nodes[_image.children[n.first]].count == 3) {
          const ImageFace section = _image.ready(n.a).face[0];
          const ImageNode seq = _image.nodes[_image.children[n.first]];
          _out.push_back({section.entry, false});
          list(section.ignoreAt, section.ignoreCount);
          list(section.hiddenAt, section.hiddenCount);
          co_await plain(_image.children[seq.first]);
          co_await plain(_image.children[seq.first + 2]);
          co_return;
        }
        break;
      case ImageKind::skip:
        list(static_cast<std::uint32_t>(n.a),
             static_cast<std::uint32_t>(n.m + n.n));
        co_await plain(_image.children[n.first]);
        co_return;
      case ImageKind::lit:
        if (n.flags & kLitGuardExpr) co_await plain(_image.children[n.first]);
        co_return;
      case ImageKind::notOf:
      case ImageKind::andOf:
        co_await conditional(_image.children[n.first]);
        co_return;
      default:
        break;
    }
    const auto children = _image.shape_children(id);
    for (std::size_t i = 0; i < children.size(); ++i)
      co_await plain(_image.shape_children(id)[i]);
  }

  // ...and where a condition reads them: a rule that is text there is no call.
  Descent<void> conditional(ImageId id) {
    const ImageNode n = _image.nodes[id];
    if (n.kind == ImageKind::embed)
      throw std::runtime_error(
          "`section<G>(open, close)` is allowed in a rule, not in a condition: a "
          "condition tests text and cannot parse a grammar -- test the token "
          "that opens the section");
    const std::size_t count = _image.shape_children(id).size();
    if (count != 0) {
      for (std::size_t i = 0; i < count; ++i)
        co_await conditional(_image.shape_children(id)[i]);
    } else if (n.kind == ImageKind::ref) {
      const ImageRuleKind kind = _image.ready(n.a).face[0].kind;
      const bool text =
          _image.rules[n.a].inlineTerminal || kind == ImageRuleKind::data;
      _out.push_back({n.a, text});
    } else {
      co_await plain(id);
    }
  }

  GrammarImage& _image;
  std::vector<Item> _queue;
  std::vector<Item> _out;
  std::unordered_set<std::uint64_t> _seen;
};

}  // namespace

WarningFound::WarningFound(GrammarImage& image)
    : _image(image), _printer(image, ImageView::resolved), _reads(image) {
  _printer.prime();
  for (const std::uint32_t rule : Reach(image).run()) {
    if (image.rules[rule].foreign) continue;
    const ImageRuleKind kind = image.ready(rule).face[1].kind;
    if (kind == ImageRuleKind::infix)
      _tables.push_back(rule);
    else if (kind != ImageRuleKind::terminal)
      _rules.push_back(rule);
  }
}

}  // namespace cursiv::engine
