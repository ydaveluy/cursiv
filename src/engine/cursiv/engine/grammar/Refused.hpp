// A grammar the generator refuses, with the message its author reads.
#pragma once

#include <stdexcept>

namespace cursiv::engine::gen {

struct Refused : std::runtime_error {
  using std::runtime_error::runtime_error;
};

}  // namespace cursiv::engine::gen
