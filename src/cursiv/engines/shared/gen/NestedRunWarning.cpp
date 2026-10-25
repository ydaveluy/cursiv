#include "cursiv/engines/shared/gen/NestedRunWarning.hpp"

#include "cursiv/engines/shared/gen/WarningFound.hpp"

namespace cursiv {

std::vector<NestedRunWarning> nested_run_warnings(GrammarImage& image) {
  return walk_warnings<NestedRunWarning>(
      image, [](WarningFound& w, ImageId id) {
        const ImageNode& n = w.image().nodes[id];
        return n.kind == ImageKind::repeat && n.n > 1 &&
               w.reads().run(w.image().kids[n.first]);
      });
}

std::string nested_run_warning_text(const NestedRunWarning& w) {
  return "rule `" + w.rule + "`: `" + w.run +
         "` repeats a run, so a run of items reads as one group or as "
         "several and the election picks one -- write one repetition unless "
         "the groups are wanted";
}

}  // namespace cursiv
