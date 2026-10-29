// What a walk over the rules of the grammar as written finds: the rules it
// reads, the name each goes by in the dump, and the dump's spelling of an
// expression.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/core/grammar/GrammarPrinter.hpp"
#include "cursiv/engines/shared/gen/WrittenReads.hpp"

namespace cursiv {

class WarningFound {
 public:
  explicit WarningFound(GrammarImage& image);

  GrammarImage& image() { return _image; }
  WrittenReads& reads() { return _reads; }
  // Terminals, operator tables and the rules of another grammar are not
  // walked; each rule comes once, in the order reached.
  const std::vector<std::uint32_t>& rules() const { return _rules; }
  // ...and the operator tables, for a walk of their operators.
  const std::vector<std::uint32_t>& tables() const { return _tables; }
  // The body of a rule, as the pipeline resolved it.
  ImageId body(std::uint32_t rule) { return _image.ready(rule).face[1].body; }

  std::string name(std::uint32_t rule) { return _printer.name_for(rule); }
  std::string spell(ImageId node) { return _printer.spell(node); }

  // Each node under `roots` once, in the order a walk of the tree reaches it
  // first: a node several paths share is read on the first. `enter(node)`
  // answers whether to walk the node's parts.
  template <class Enter>
  void each_node(std::span<const ImageId> roots, Enter enter) {
    ++_walk;
    _todo.assign(roots.rbegin(), roots.rend());
    while (!_todo.empty()) {
      const ImageId id = _todo.back();
      _todo.pop_back();
      if (id >= _seen.size())
        _seen.resize(std::max<std::size_t>(id + 1u, _image.nodes.size()));
      if (_seen[id] == _walk) continue;
      _seen[id] = _walk;
      if (!enter(id)) continue;
      const std::span<const ImageId> kids = _image.shape_kids(id);
      _todo.insert(_todo.end(), kids.rbegin(), kids.rend());
    }
  }

 private:
  GrammarImage& _image;
  GrammarPrinter _printer;
  WrittenReads _reads;
  std::vector<std::uint32_t> _rules;
  std::vector<std::uint32_t> _tables;
  // The walk in hand, and the last walk that read each node.
  std::uint32_t _walk = 0;
  std::vector<std::uint32_t> _seen;
  std::vector<ImageId> _todo;
};

// Each branch's place among the branches of `choice` as the dump prints them,
// from one: `implied` prints a branch and the next as one.
inline std::vector<std::uint16_t> printed_places(const GrammarImage& image,
                                                 const ImageNode& choice) {
  std::vector<std::uint16_t> out(choice.count);
  std::uint16_t place = 0;
  for (std::uint32_t i = 0; i < choice.count; ++i) {
    const ImageId* const arm = &image.kids[choice.first + i];
    const bool merged = i + 1 < choice.count &&
                        image.nodes[arm[1]].kind == ImageKind::implied &&
                        image.kids[image.nodes[arm[1]].first] == arm[0];
    out[i] = merged ? place : ++place;
  }
  return out;
}

// `W{rule, spelling}` for each node `hit(found, node)` names under the rules
// walked, once each.
template <class W, class Hit>
std::vector<W> walk_warnings(WarningFound& found, Hit hit) {
  std::vector<W> out;
  for (const std::uint32_t rule : found.rules()) {
    const std::string name = found.name(rule);
    const ImageId body = found.body(rule);
    found.each_node({&body, 1}, [&](ImageId id) {
      if (hit(found, id)) {
        W w{name, found.spell(id)};
        if (std::ranges::find(out, w) == out.end()) out.push_back(std::move(w));
      }
      return true;
    });
  }
  return out;
}

}  // namespace cursiv
