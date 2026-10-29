#include "cursiv/engines/shared/gen/NestedRunWarning.hpp"

#include "cursiv/engines/shared/gen/WarningFound.hpp"

namespace cursiv {

std::vector<NestedRunWarning> nested_run_warnings(WarningFound& found) {
  return walk_warnings<NestedRunWarning>(
      found, [](WarningFound& w, ImageId id) {
        const ImageNode& n = w.image().nodes[id];
        return n.kind == ImageKind::repeat && n.n > 1 &&
               w.reads().run(w.image().kids[n.first]);
      });
}

std::string nested_run_warning_text(const NestedRunWarning& w) {
  return "rule `" + w.rule + "`: `" + w.run +
         "` repeats a repetition, so a run of items can be split into "
         "groups in several ways and the parser picks one -- write one "
         "repetition unless the groups matter";
}

}  // namespace cursiv
