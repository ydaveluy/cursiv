// A refused rule as its author wrote it, found back by the name the pool
// holds: what a refusal shows under its sentence.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/dump.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv {

template <class Rules>
struct AsWritten;
template <class... Rs>
struct AsWritten<TypeList<Rs...>> {
  static std::string of(std::string_view name) {
    std::string out;
    ((out.empty() && detail::type_name<declared_of_t<Rs>>() == name
          ? out = "\n    " + dump_rule<declared_of_t<Rs>>()
          : out),
     ...);
    return out;
  }
};

}  // namespace cursiv
