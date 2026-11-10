// A refused rule as its author wrote it, found by the name the pool holds:
// what a refusal shows under its sentence.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/dump.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

template <class Rules>
struct AsWritten;
template <class... Rs>
struct AsWritten<TypeList<Rs...>> {
  static std::string of(std::string_view name) {
    std::string out;
    ((out.empty() && type_name<declared_of_t<Rs>>() == name
          ? out = "\n    " + dump_rule<declared_of_t<Rs>>()
          : out),
     ...);
    return out;
  }
};

}  // namespace cursiv::engine
