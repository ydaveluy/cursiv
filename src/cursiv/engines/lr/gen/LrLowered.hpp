// The whole output of the lowering: the context-free core, and what the tree
// owes at each of its positions.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/InfixOp.hpp"
#include "cursiv/engines/shared/gen/optional_shape.hpp"

namespace cursiv {

struct GrammarImage;
struct LowerDesc;

struct LrLowered {
  LrBuildCfg cfg{};
  // The optional positions an alternative spells out as subsets.
  unsigned optionalGate = kOptionalGate;
  ChainPool writes{};
  // Here and not in the core, which the automaton copies.
  GrammarFault fault{};
  // The nonterminals the fault's cycle runs through, from its own.
  std::vector<std::uint32_t> faultCycle{};
  // The auxiliaries the optional gate made of surplus optional positions.
  std::vector<std::uint16_t> gated{};
  // Every operator of every precedence table, bound to its opening tokens
  // once the productions are laid.
  std::vector<InfixOp> infixOps{};
};

LrLowered lr_lower(GrammarImage& image, const LowerDesc& desc);

}  // namespace cursiv
