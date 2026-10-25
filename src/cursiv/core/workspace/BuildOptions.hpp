#pragma once

#include <vector>

// What one build is asked to do, with only the fields whose services exist.

namespace cursiv {

struct ValidationCategory;

// What a build was asked for, remembered per document: a document built with
// one set of categories keeps it across the updates that follow, so a client
// that configures validation per language does not have to say so again on
// every keystroke.
struct BuildOptions {
  // Whether the build runs validation. If false, a newly built document stops
  // at IndexedReferences; completing the request does not imply Validated.
  bool validation = true;

  // Which categories the validating phase runs; empty is every one. Owned, not
  // a span: the builder keeps this past the call that handed it over.
  std::vector<const ValidationCategory*> validationCategories;
};

}  // namespace cursiv
