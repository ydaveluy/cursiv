// The LR lowering of a grammar image: every rule walked under each skip
// configuration that reads it, and its productions written. The grammar's types
// reach it only through `LowerDesc`.
#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/lr/gen/LrLowerPool.hpp"
#include "cursiv/lr/gen/LrLowered.hpp"
#include "cursiv/shared/gen/Lowerer.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

class LrLowerer : public LowerWalk<LrLowerer, LrLowered, LrLowerPool> {
 public:
  using LowerWalk::LowerWalk;

  LrLowered run();

 private:
  friend LowerWalk;

  std::uint16_t intern_name(std::string_view text) override;
  Descent<ImageId> ended(ImageId id) override;
  void declare_start() override;
  void declare_rules() override;

  Descent<LrShapeId> build(unsigned cfg, ImageId e);
  Descent<LrShapeId> build_close(unsigned cfg, unsigned in, ImageId e);
  // The call of a grammar is its configuration's entry.
  void enter(unsigned cfg, LrShapeId at) {
    _cfg.cfgEntry[cfg] = nt_index(static_cast<std::uint16_t>(_p.e[at].sym));
  }
  Descent<LrShapeId> build_repeat(unsigned cfg, ImageId e);
  Descent<LrShapeId> build_skip(unsigned cfg, ImageId e);
  Descent<LrShapeId> build_made(unsigned cfg, ImageId e);
  Descent<LrShapeId> build_write(unsigned cfg, ImageId e);
  Descent<LrShapeId> build_flag(unsigned cfg, ImageId e);
  LrShapeId build_condition(unsigned cfg, ImageId e);
  std::uint16_t stop_rule(DynSet terms, std::uint8_t trailing);
  LrShapeId build_stop(unsigned cfg, ImageId e, const Cut& cut);
  std::uint16_t closing_stop(std::uint16_t cut, std::uint8_t trailing);
  Descent<LrShapeId> build_implied(unsigned cfg, ImageId e);
  Descent<LrShapeId> capture_created(LrShapeId at);
  LrShapeId once_site(LrShapeId at) const;
  bool is_condition(LrShapeId at) const;
  Descent<void> ambient(LrShapeId at, bool open, std::uint8_t edge);
  std::uint16_t open_tail(std::uint16_t reading, std::uint8_t tail);

  void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                  std::uint8_t tail) override;
  bool define_open() override;
  bool open_one(unsigned cfg, std::uint32_t rule, std::uint16_t reading);
  void define_infix(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void emit_op(unsigned cfg, std::uint16_t lhs, ImageId op, const void* opRow,
               const LrLowerSlot& left, const LrLowerSlot& right,
               const LrActs& node, std::uint8_t prec, Association group);
  LrLowerSlot infix_side(unsigned cfg, const void* row,
                         std::uint16_t sym) const;

  std::unordered_map<ImageId, ImageId> _ended;
};

}  // namespace cursiv::engine
