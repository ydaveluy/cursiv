#include "cursiv/engines/lr/gen/lr_fault_message.hpp"

#include <vector>

#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

// The rule an author wrote that `nt` stands for: an auxiliary is told on its
// owner.
static std::uint16_t lr_authored(const LrBuildCfg& c, std::uint16_t nt) {
  if (nt >= c.nnts || nt < c.nnts - c.auxCount) return nt;
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    const std::uint16_t own = c.prodBuild[k].owner;
    if (own >= c.nnts || own == nt) continue;
    if (q.lhs == nt) return own;
    for (unsigned i = 0; i < q.len; ++i) {
      const std::uint16_t y = c.sym_at(q, i);
      if (!is_term(y) && nt_index(y) == nt) return own;
    }
  }
  return nt;
}

// The rule a refusal names.
std::uint16_t lr_fault_rule(const LrLowered& o) {
  return lr_authored(o.cfg, o.fault.rule);
}

static std::string lr_fault_shape(const LrLowered& o) {
  const auto& c = o.cfg;
  const std::uint16_t nt = lr_fault_rule(o);
  std::string out;
  unsigned shown = 0;
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    if (q.lhs != nt && c.prodBuild[k].owner != nt) continue;
    if (shown == 8) return out + "\n    ...";
    out += "\n    " + c.prod_text(q);
    if (q.len == 0) out += " <nothing>";
    ++shown;
  }
  return out;
}

std::string lr_fault_message(const LrLowered& o,
                                    std::string_view written) {
  const std::uint16_t nt = lr_fault_rule(o);
  // An auxiliary reads as its owner, once however many follow in a row.
  std::vector<std::string_view> cycle;
  for (const auto x : o.faultCycle) {
    const auto name =
        o.cfg.nt_name(lr_authored(o.cfg, static_cast<std::uint16_t>(x)));
    if (cycle.empty() || cycle.back() != name) cycle.push_back(name);
  }
  while (cycle.size() > 1 && cycle.back() == cycle.front()) cycle.pop_back();
  return grammar_fault_message(
      o.fault, nt < o.cfg.nnts ? o.cfg.nt_name(nt) : std::string_view{},
      written, lr_fault_shape(o) + grammar_fault_cycle(cycle));
}

}  // namespace cursiv
