#include <pegium/core/grammar/TerminalRule.hpp>
#include <pegium/core/grammar/PrintUtils.hpp>

#include <ostream>

namespace pegium::grammar {

void TerminalRule::print(std::ostream &os) const {
  detail::print_rule(os, *this, "terminal ");
}

} // namespace pegium::grammar
