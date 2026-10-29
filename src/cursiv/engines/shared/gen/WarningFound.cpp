#include "cursiv/engines/shared/gen/WarningFound.hpp"

#include "cursiv/engines/shared/gen/ReachedRules.hpp"

namespace cursiv {

WarningFound::WarningFound(GrammarImage& image)
    : _image(image), _printer(image, ImageView::resolved), _reads(image) {
  _printer.prime();
  for (const std::uint32_t rule : reached_rules(image).visible) {
    if (image.rules[rule].foreign) continue;
    const ImageRuleKind kind = image.ready(rule).face[1].kind;
    if (kind == ImageRuleKind::infix)
      _tables.push_back(rule);
    else if (kind != ImageRuleKind::terminal)
      _rules.push_back(rule);
  }
}

ImageId WarningFound::body(std::uint32_t rule) {
  return _image.ready(rule).face[1].body;
}

}  // namespace cursiv
