// What one helper-built document is asked for: a build request, plus the name
// it is filed under.
#pragma once

#include <string>

#include "cursiv/core/workspace/BuildOptions.hpp"

namespace cursiv::test {

struct ParseHelperOptions : BuildOptions {
  // Empty picks a counter and the language's first file extension.
  std::string documentUri;
};

}  // namespace cursiv::test
