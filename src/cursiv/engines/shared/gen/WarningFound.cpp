#include "cursiv/engines/shared/gen/WarningFound.hpp"

#include <cstddef>
#include <stdexcept>
#include <unordered_set>

namespace cursiv {

namespace {

struct Item {
  std::uint32_t rule = 0;
  bool condition = false;
};

// The rules a grammar reaches from `entry`, `ignored` and `hidden`, in the
// order reached, closed over the rules each body names as the pipeline resolved
// the bodies. A rule a condition names is read there as text, never as a call:
// it is reached, and not one of the rules read as themselves.
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

  void call(std::uint32_t rule) {
    if (_image.rules[rule].grammar)
      throw std::runtime_error(
          "`ref<G>` names a grammar: read it with `section<G>(open, close)`, "
          "which puts G's text between two tokens of the caller");
    _out.push_back({rule, false});
  }

  void collect(std::uint32_t rule) {
    const ImageFace f = _image.ready(rule).face[1];
    if (f.kind == ImageRuleKind::infix) {
      _out.push_back({f.primary, false});
      for (std::uint32_t i = 0; i < f.levelCount; ++i)
        plain(_image.levels[f.levelsAt + i].op);
    } else if (f.kind != ImageRuleKind::grammar) {
      plain(f.body);
    }
  }

  // The rules an expression names where it is read.
  void plain(ImageId id) {
    const ImageNode n = _image.nodes[id];
    switch (n.kind) {
      case ImageKind::ref:
        call(n.a);
        return;
      case ImageKind::embed:
        if (_image.nodes[_image.kids[n.first]].kind == ImageKind::seq &&
            _image.nodes[_image.kids[n.first]].count == 3) {
          const ImageFace section = _image.ready(n.a).face[0];
          const ImageNode seq = _image.nodes[_image.kids[n.first]];
          _out.push_back({section.entry, false});
          list(section.ignoreAt, section.ignoreCount);
          list(section.hiddenAt, section.hiddenCount);
          plain(_image.kids[seq.first]);
          plain(_image.kids[seq.first + 2]);
          return;
        }
        break;
      case ImageKind::skip:
        list(static_cast<std::uint32_t>(n.a),
             static_cast<std::uint32_t>(n.m + n.n));
        plain(_image.kids[n.first]);
        return;
      case ImageKind::lit:
        if (n.flags & kLitGuardExpr) plain(_image.kids[n.first]);
        return;
      case ImageKind::notOf:
      case ImageKind::andOf:
        conditional(_image.kids[n.first]);
        return;
      default:
        break;
    }
    const auto kids = _image.shape_kids(id);
    for (std::size_t i = 0; i < kids.size(); ++i)
      plain(_image.shape_kids(id)[i]);
  }

  // ...and where a condition reads them: a rule that is text there is no call.
  void conditional(ImageId id) {
    const ImageNode n = _image.nodes[id];
    if (n.kind == ImageKind::embed)
      throw std::runtime_error(
          "`section<G>(open, close)` stands in a rule, not in a condition: a "
          "condition reads text and never parses a grammar -- ask the token "
          "that opens the section");
    const std::size_t count = _image.shape_kids(id).size();
    if (count != 0) {
      for (std::size_t i = 0; i < count; ++i)
        conditional(_image.shape_kids(id)[i]);
    } else if (n.kind == ImageKind::ref) {
      const ImageRuleKind kind = _image.ready(n.a).face[0].kind;
      if (_image.rules[n.a].inlineTerminal || kind == ImageRuleKind::data)
        _out.push_back({n.a, true});
      else
        call(n.a);
    } else {
      plain(id);
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

}  // namespace cursiv
