// What a walk over the rules of the grammar as written finds: the rules it
// reads, the name each goes by in the dump, and the dump's spelling of an
// expression.
#pragma once

#include <algorithm>
#include <cstdint>
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
  // The body of a rule, as the pipeline resolved it.
  ImageId body(std::uint32_t rule);

  std::string name(std::uint32_t rule) { return _printer.name_for(rule); }
  std::string spell(ImageId node) { return _printer.spell(node); }

 private:
  GrammarImage& _image;
  GrammarPrinter _printer;
  WrittenReads _reads;
  std::vector<std::uint32_t> _rules;
};

// `W{rule, spelling}` for each node `hit(found, node)` names under the rules
// walked, once each.
template <class W, class Hit>
std::vector<W> walk_warnings(GrammarImage& image, Hit hit) {
  WarningFound found(image);
  std::vector<W> out;
  const auto walk = [&](auto& self, const std::string& rule,
                        ImageId id) -> void {
    if (hit(found, id)) {
      W w{rule, found.spell(id)};
      if (std::ranges::find(out, w) == out.end()) out.push_back(std::move(w));
    }
    for (const ImageId kid : image.shape_kids(id)) self(self, rule, kid);
  };
  for (const std::uint32_t rule : found.rules())
    walk(walk, found.name(rule), found.body(rule));
  return out;
}

}  // namespace cursiv
