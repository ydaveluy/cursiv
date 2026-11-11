#include "cursiv/shared/gen/WarningFound.hpp"

#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

WarningFound::WarningFound(GrammarImage& image,
                           std::span<const ImageRuleDesc* const> reached)
    : _image(image), _printer(image, ImageView::resolved), _reads(image) {
  _printer.prime();
  for (const ImageRuleDesc* const d : reached) {
    const std::uint32_t rule = image.rule(*d);
    if (image.rules[rule].foreign) continue;
    const ImageRuleKind kind = image.ready(rule).face[1].kind;
    if (kind == ImageRuleKind::infix)
      _tables.push_back(rule);
    else if (kind != ImageRuleKind::terminal)
      _rules.push_back(rule);
  }
}

}  // namespace cursiv::engine
