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

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/shared/gen/WrittenReads.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/GrammarPrinter.hpp"
#include "cursiv/shared/utils/RoundMarks.hpp"

namespace cursiv::engine {

class WarningFound {
 public:
  // `reached` are the rules the grammar reaches, in the order reached.
  WarningFound(GrammarImage& image,
               std::span<const ImageRuleDesc* const> reached);

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
  // returns whether to walk the node's parts.
  template <class Enter>
  void each_node(std::span<const ImageId> roots, Enter enter) {
    _seen.next();
    _todo.assign(roots.rbegin(), roots.rend());
    while (!_todo.empty()) {
      const ImageId id = _todo.back();
      _todo.pop_back();
      if (!_seen.first(id)) continue;
      if (!enter(id)) continue;
      const std::span<const ImageId> children = _image.shape_children(id);
      _todo.insert(_todo.end(), children.rbegin(), children.rend());
    }
  }

 private:
  GrammarImage& _image;
  GrammarPrinter _printer;
  WrittenReads _reads;
  std::vector<std::uint32_t> _rules;
  std::vector<std::uint32_t> _tables;
  // The walk in hand, and the last walk that read each node.
  RoundMarks _seen;
  std::vector<ImageId> _todo;
};

// Each branch's place among the branches of `choice` as the dump prints them,
// from one: `implied` prints a branch and the next as one.
inline std::vector<std::uint16_t> printed_places(const GrammarImage& image,
                                                 const ImageNode& choice) {
  std::vector<std::uint16_t> out(choice.count);
  std::uint16_t place = 0;
  for (std::uint32_t i = 0; i < choice.count; ++i) {
    const ImageId* const arm = &image.children[choice.first + i];
    const bool merged = i + 1 < choice.count &&
                        image.nodes[arm[1]].kind == ImageKind::implied &&
                        image.children[image.nodes[arm[1]].first] == arm[0];
    out[i] = merged ? place + 1 : ++place;
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

}  // namespace cursiv::engine
