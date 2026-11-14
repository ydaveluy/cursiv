#pragma once

#include <vector>

#include "cursiv/validation/ValidationCategory.hpp"

// What one build is asked to do, with only the fields whose services exist.

namespace cursiv {

// What a build was asked for, remembered per document across the updates that
// follow.
struct BuildOptions {
  // Whether the build runs validation. If false, a newly built document stops
  // at IndexedReferences; completing the request does not imply Validated.
  bool validation = true;

  // Which categories the validating phase runs; empty is every one. Owned, not
  // a span: the builder keeps this past the call that handed it over.
  std::vector<const ValidationCategory*> validationCategories;
};

namespace detail {

// What the first scan and every update ask by default: the checks that keep
// up with typing.
[[nodiscard]] inline BuildOptions typing_build_options() {
  return {true, {&built_in, &fast}};
}

}  // namespace detail

}  // namespace cursiv
