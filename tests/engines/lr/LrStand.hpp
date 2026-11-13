// A generated LR drive standing before its first byte, for recovery oracles.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrDrive.hpp"
#include "cursiv/lr/runtime/LrParser.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"

namespace checks {

inline constexpr cursiv::engine::RepairBudget kUnbounded{
    .floor = ~std::uint64_t{0},
    .perByte = 0,
    .perSearch = ~std::uint64_t{0},
    .perParse = ~std::uint64_t{0}};

template <const auto& A>
struct Stand {
  cursiv::Cst cst;
  cursiv::detail::CstBuilder builder;
  cursiv::engine::LrContext c;
  std::vector<cursiv::engine::Scan> room;

  Stand(std::string_view text, const cursiv::engine::LrDrive& drive)
      : cst(text),
        builder(cst),
        c(cst.text(), builder),
        room(A.maxCands != 0 ? A.maxCands : 1) {
    builder.reserve_stream(cst.text().size());
    c.cand = room.data();
    c.drive = &drive;
    cursiv::engine::LrParser<A>::install_hooks(c);
    c.stack.push({0, cursiv::kNoElement, 0, 0, cursiv::engine::kLrNoWord});
  }
};

}  // namespace checks
