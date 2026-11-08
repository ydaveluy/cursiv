// A name, literal or delimiter added to the build's text pool, refused where
// the pool cannot hold it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "cursiv/lr/gen/LrBuildCfg.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"

namespace cursiv::engine {

inline std::uint16_t lr_intern_name(LrBuildCfg& c, std::string_view text) {
  if (text.empty()) return c.nameLen;
  if (text.size() > 0xffffu - c.nameLen)
    throw LowerRefused(LowerFail::namePool);
  const std::uint16_t off = c.nameLen;
  std::ranges::copy(text, c.names.row(off, text.size()));
  c.nameLen = static_cast<std::uint16_t>(off + text.size());
  return off;
}

}  // namespace cursiv::engine
