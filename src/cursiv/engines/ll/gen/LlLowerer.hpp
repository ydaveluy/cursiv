// The LL lowering of a grammar image, in ordinary code: every rule walked
// under each skip configuration that reads it, kept as the shape its author
// wrote and emitted as the nodes of a descent. The grammar's types reach it as
// the constant descriptors of `LowerDesc` and nothing else.
#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/ll/gen/LlLowerPool.hpp"
#include "cursiv/engines/ll/gen/LlLowered.hpp"
#include "cursiv/engines/shared/gen/Lowerer.hpp"

namespace cursiv {

class LlLowerer : public LowerWalk<LlLowerer, LlLowered, detail::llow::Pool> {
 public:
  using LowerWalk::LowerWalk;

  LlLowered run();

 private:
  friend LowerWalk;

  std::uint16_t intern_name(std::string_view text) override;
  ImageId ended(ImageId id) override;
  void declare_start() override;
  void declare_rules() override;

  ImageId ended_in(ImageId id, bool inPredicate);
  std::uint16_t lay_operator(unsigned cfg, ImageId op,
                             std::vector<std::uint32_t>& reading);

  std::uint16_t lay(unsigned cfg, ImageId e);
  // An embed's close, read under the caller's configuration.
  std::uint16_t lay_close(unsigned cfg, unsigned, ImageId e) {
    return lay(cfg, e);
  }
  // The call of a grammar opens its section, its configuration plus one.
  void enter(unsigned cfg, std::uint16_t at) {
    _p.e[at].section = static_cast<std::uint8_t>(cfg + 1u);
  }
  std::uint16_t lay_repeat(unsigned cfg, ImageId e);
  std::uint16_t lay_create(unsigned cfg, ImageId e);
  std::uint16_t lay_nest(unsigned cfg, ImageId e);
  std::uint16_t lay_closed(unsigned cfg, ImageId e);
  std::uint16_t lay_write(unsigned cfg, ImageId e);
  std::uint16_t lay_flag(unsigned cfg, ImageId e);
  std::uint16_t lay_condition(unsigned cfg, ImageId e);
  std::uint16_t lay_implied(unsigned cfg, ImageId e);

  void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                  std::uint8_t tail) override;
  void emit_climb(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void emit_op(unsigned cfg, ImageId op, const void* opRow, std::uint8_t prec,
               Association group);
  std::uint16_t climb_chain(const void* row);
  void note_nests();

  std::unordered_map<std::uint64_t, ImageId> _endedIn;
};

}  // namespace cursiv
