// A type as the emitted unit must read it back. The unit is written at global
// scope, so a name is qualified from there -- its own namespace would shadow
// the author's (`json::generated::JSON::json`).
#pragma once

#include <string>
#include <string_view>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

// Template arguments stay as written: the unit is emitted at global scope, so
// they resolve there.
inline std::string gen_qualified(std::string_view name) {
  return name == "void" || name.starts_with("::") ? std::string(name)
                                                  : "::" + std::string(name);
}

// The narrowings are spelled qualified already.
inline std::string narrow_list(const auto& narrows) {
  return concat("::cursiv::TypeList<", joined(narrows, ", "), ">");
}

}  // namespace cursiv::engine
