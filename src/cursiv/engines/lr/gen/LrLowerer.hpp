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

struct LrLowerState {
  LrLowered lowered{};
  LrLowerPool pool{};
};

class LrLowerer : private LrLowerState, public Lowerer {
 public:
  LrLowerer(GrammarImage& image, const LowerDesc& desc);

  LrLowered run();

 private:
  std::uint16_t intern_name(std::string_view text) override;
  ImageId ended(ImageId id) override;
  void declare_start() override;
  void declare_rules() override;

  LrShapeId lay(unsigned cfg, ImageId e);
  LrShapeId lay_ref(unsigned cfg, std::uint32_t rule);
  LrShapeId lay_embed(unsigned cfg, ImageId e);
  LrShapeId lay_seq(unsigned cfg, ImageId e);
  LrShapeId lay_kids(unsigned cfg, LrShape kind,
                     const std::vector<ImageId>& es);
  LrShapeId gather(unsigned cfg, LrShape kind,
                   const std::vector<LrShapeId>& kids);
  LrShapeId lay_repeat(unsigned cfg, ImageId e);
  LrShapeId lay_skip(unsigned cfg, ImageId e);
  LrShapeId lay_made(unsigned cfg, ImageId e);
  LrShapeId lay_write(unsigned cfg, ImageId e);
  LrShapeId lay_flag(unsigned cfg, ImageId e);
  LrShapeId lay_gap(unsigned cfg, ImageId e);
  LrShapeId lay_stop(unsigned cfg, ImageId e, const Cut& cut);
  LrShapeId lay_implied(unsigned cfg, ImageId e);
  LrShapeId mark(unsigned cfg, LrShapeId at);
  LrShapeId capture_created(LrShapeId at);
  LrShapeId once_site(LrShapeId at) const;
  void ambient(LrShapeId at, bool open, std::uint8_t edge);
  std::uint16_t open_tail(std::uint16_t reading, std::uint8_t tail);

  void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                  std::uint8_t tail) override;
  bool define_open() override;
  void define_infix(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void emit_op(unsigned cfg, std::uint16_t lhs, ImageId op, const void* opRow,
               const LrLowerSlot& left, const LrLowerSlot& right,
               const LrActs& node, std::uint8_t prec, Association group);
  LrLowerSlot infix_side(unsigned cfg, const void* row, std::uint16_t sym);
  bool open_one(unsigned cfg, std::uint32_t rule, std::uint16_t reading);

  LrLowered& _o;
  LrLowerPool& _p;
  LrBuildCfg& _cfg;
};

}  // namespace cursiv
