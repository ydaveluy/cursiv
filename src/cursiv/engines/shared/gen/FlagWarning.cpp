#include "cursiv/engines/shared/gen/FlagWarning.hpp"

#include "cursiv/engines/shared/gen/WarningFound.hpp"

namespace cursiv {

std::vector<FlagWarning> flag_warnings(GrammarImage& image) {
  return walk_warnings<FlagWarning>(image, [](WarningFound& w, ImageId id) {
    const ImageNode& n = w.image().nodes[id];
    return n.kind == ImageKind::flag &&
           w.reads().nothing(w.image().kids[n.first]);
  });
}

std::string flag_warning_text(const FlagWarning& w) {
  return "rule `" + w.rule + "`: `" + w.flag +
         "` reads an operand that may match nothing, so it always sets the "
         "member -- flag the token that tells, and make the flag optional "
         "(`option(flag<&T::m>(kw<\"on\">))`)";
}

}  // namespace cursiv
