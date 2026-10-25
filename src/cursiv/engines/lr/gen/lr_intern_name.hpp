// A name, literal or delimiter added to the build's text pool, refused where
// the pool cannot hold it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"

namespace cursiv {

inline std::uint16_t lr_intern_name(LrBuildCfg& c, std::string_view text) {
  if (text.empty()) return c.nameLen;
  if (text.size() > 0xffffu - c.nameLen) {
    lower_refuse(c, LowerFail::namePool);
    throw LowerRefused(LowerFail::namePool);
  }
  const std::uint16_t off = c.nameLen;
  std::copy(text.begin(), text.end(), c.names.row(off, text.size()));
  c.nameLen = static_cast<std::uint16_t>(off + text.size());
  return off;
}

}  // namespace cursiv
