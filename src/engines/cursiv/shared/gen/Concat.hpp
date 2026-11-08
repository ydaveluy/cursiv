// A value of generated code, piece by piece: each value stands where it goes,
// and the braces of the code stay as written.
#pragma once

#include <string>
#include <utility>

#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

template <class... Parts>
[[nodiscard]] std::string concat(const Parts&... parts) {
  TextBuilder out;
  out.write(parts...);
  return out.take();
}

}  // namespace cursiv::engine
