// The LR lowering of a grammar image, in ordinary code: every rule walked
// under each skip configuration that reads it, and the productions laid. The
// grammar's types reach it as the constant descriptors of `LowerDesc` and
// nothing else.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/engines/lr/gen/LrLowerPool.hpp"
#include "cursiv/engines/lr/gen/LrLowered.hpp"
#include "cursiv/engines/shared/gen/Lowerer.hpp"

namespace cursiv {

class LrLowerer : public LowerWalk<LrLowerer, LrLowered, LrLowerPool> {
 public:
  using LowerWalk::LowerWalk;

  LrLowered run();

 private:
  friend LowerWalk;

  std::uint16_t intern_name(std::string_view text) override;
  ImageId ended(ImageId id) override;
  void declare_start() override;
  void declare_rules() override;

  LrShapeId lay(unsigned cfg, ImageId e);
  LrShapeId lay_close(unsigned cfg, unsigned in, ImageId e);
  // The call of a grammar is its configuration's entry.
  void enter(unsigned cfg, LrShapeId at) {
    _cfg.cfgEntry[cfg] = nt_index(static_cast<std::uint16_t>(_p.e[at].sym));
  }
  LrShapeId lay_repeat(unsigned cfg, ImageId e);
  LrShapeId lay_skip(unsigned cfg, ImageId e);
  LrShapeId lay_made(unsigned cfg, ImageId e);
  LrShapeId lay_write(unsigned cfg, ImageId e);
  LrShapeId lay_flag(unsigned cfg, ImageId e);
  LrShapeId lay_condition(unsigned cfg, ImageId e);
  LrShapeId lay_stop(unsigned cfg, ImageId e, const Cut& cut);
  LrShapeId lay_implied(unsigned cfg, ImageId e);
  LrShapeId capture_created(LrShapeId at);
  LrShapeId once_site(LrShapeId at) const;
  bool is_condition(LrShapeId at) const;
  void ambient(LrShapeId at, bool open, std::uint8_t edge);
  std::uint16_t open_tail(std::uint16_t reading, std::uint8_t tail);

  void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                  std::uint8_t tail) override;
  bool define_open() override;
  bool open_one(unsigned cfg, std::uint32_t rule, std::uint16_t reading);
  void define_infix(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void emit_op(unsigned cfg, std::uint16_t lhs, ImageId op, const void* opRow,
               const LrLowerSlot& left, const LrLowerSlot& right,
               const LrActs& node, std::uint8_t prec, Association group);
  LrLowerSlot infix_side(unsigned cfg, const void* row, std::uint16_t sym);
};

}  // namespace cursiv
