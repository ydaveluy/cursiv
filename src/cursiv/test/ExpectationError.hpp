// What a failed expectation throws when no harness installed its own hook.
#pragma once

#include <stdexcept>

namespace cursiv::test {

class ExpectationError : public std::logic_error {
 public:
  using std::logic_error::logic_error;
};

}  // namespace cursiv::test
