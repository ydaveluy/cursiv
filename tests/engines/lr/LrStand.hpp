// A generated LR drive standing before its first byte, for recovery oracles.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"

namespace checks {

inline constexpr cursiv::RepairBudget kUnbounded{.floor = ~std::uint64_t{0},
                                                .perByte = 0,
                                                .perSearch = ~std::uint64_t{0},
                                                .perParse = ~std::uint64_t{0}};

template <const auto& A>
struct Stand {
  cursiv::Cst cst;
  cursiv::CstBuilder builder;
  cursiv::LrContext c;
  std::vector<cursiv::Scan> room;

  Stand(std::string_view text, const cursiv::LrDrive& drive)
      : cst(text), builder(cst), c(cst.text(), builder),
        room(A.maxCands != 0 ? A.maxCands : 1) {
    builder.reserve_stream(cst.text().size());
    c.cand = room.data();
    c.drive = &drive;
    cursiv::LrParser<A>::install_hooks(c);
    c.stack.push({0, cursiv::kNoElement, 0, 0, cursiv::kLrNoWord});
  }
};

}  // namespace checks
