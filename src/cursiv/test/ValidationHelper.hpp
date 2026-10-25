// One text, built with its checks run.
#pragma once

#include <string>

#include "cursiv/test/ParseHelperOptions.hpp"
#include "cursiv/test/ValidationResult.hpp"

namespace cursiv {
struct CoreServices;
}  // namespace cursiv

namespace cursiv::test {

struct ValidationHelperOptions : ParseHelperOptions {
  // Whether a syntax error is a failure of the helper rather than a finding.
  bool failOnParsingErrors = false;
};

[[nodiscard]] ValidationResult validationHelper(
    const CoreServices& services, std::string text,
    const ValidationHelperOptions& options = {});

}  // namespace cursiv::test
