// The LL lowering of a grammar image, in ordinary code: every rule walked
// under each skip configuration that reads it, kept as the shape its author
// wrote and emitted as the nodes of a descent. The grammar's types reach it as
// the constant descriptors of `LowerDesc` and nothing else.
#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/ll/gen/LlLowerPool.hpp"
#include "cursiv/ll/gen/LlLowered.hpp"
#include "cursiv/shared/gen/Lowerer.hpp"

namespace cursiv {

class LlLowerer : public LowerWalk<LlLowerer, LlLowered, detail::llow::Pool> {
 public:
  using LowerWalk::LowerWalk;

  LlLowered run();

 private:
  friend LowerWalk;

  std::uint16_t intern_name(std::string_view text) override;
  detail::ImageId ended(detail::ImageId id) override;
  void declare_start() override;
  void declare_rules() override;

  detail::ImageId ended_in(detail::ImageId id, bool inPredicate);
  std::uint16_t lay_operator(unsigned cfg, detail::ImageId op,
                             std::vector<std::uint32_t>& reading);

  std::uint16_t lay(unsigned cfg, detail::ImageId e);
  // An embed's close, read under the caller's configuration.
  std::uint16_t lay_close(unsigned cfg, unsigned, detail::ImageId e) {
    return lay(cfg, e);
  }
  // The call of a grammar opens its section, its configuration plus one.
  void enter(unsigned cfg, std::uint16_t at) {
    _p.e[at].section = static_cast<std::uint8_t>(cfg + 1u);
  }
  std::uint16_t lay_repeat(unsigned cfg, detail::ImageId e);
  std::uint16_t lay_create(unsigned cfg, detail::ImageId e);
  std::uint16_t lay_nest(unsigned cfg, detail::ImageId e);
  std::uint16_t lay_closed(unsigned cfg, detail::ImageId e);
  std::uint16_t lay_write(unsigned cfg, detail::ImageId e);
  std::uint16_t lay_flag(unsigned cfg, detail::ImageId e);
  std::uint16_t lay_condition(unsigned cfg, detail::ImageId e);
  std::uint16_t lay_implied(unsigned cfg, detail::ImageId e);

  void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                  std::uint8_t tail) override;
  void emit_climb(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void emit_op(unsigned cfg, detail::ImageId op, const void* opRow,
               std::uint8_t prec, Association group);
  std::uint16_t climb_chain(const void* row);
  void note_nests();

  std::unordered_map<std::uint64_t, detail::ImageId> _endedIn;
};

}  // namespace cursiv
