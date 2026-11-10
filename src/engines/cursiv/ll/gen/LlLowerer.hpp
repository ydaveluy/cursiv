// The LL lowering of a grammar image: every rule walked under each skip
// configuration that reads it, kept as written and emitted as the nodes of a
// descent. The grammar's types reach it only through `LowerDesc`.
#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/ll/gen/LlLowerPool.hpp"
#include "cursiv/ll/gen/LlLowered.hpp"
#include "cursiv/shared/gen/Lowerer.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

class LlLowerer : public LowerWalk<LlLowerer, LlLowered, llow::Pool> {
 public:
  using LowerWalk::LowerWalk;

  LlLowered run();

 private:
  friend LowerWalk;

  std::uint16_t intern_name(std::string_view text) override;
  Descent<ImageId> ended(ImageId id) override;
  void declare_start() override;
  void declare_rules() override;

  Descent<ImageId> ended_in(ImageId id, bool inPredicate);
  Descent<std::uint16_t> build_operator(unsigned cfg, ImageId op,
                                        std::vector<std::uint32_t>& reading);

  Descent<std::uint16_t> build(unsigned cfg, ImageId e);
  // An embed's close, read under the caller's configuration.
  Descent<std::uint16_t> build_close(unsigned cfg, unsigned, ImageId e) {
    return build(cfg, e);
  }
  // The call of a grammar opens its section, its configuration plus one.
  void enter(unsigned cfg, std::uint16_t at) {
    _p.e[at].section = static_cast<std::uint8_t>(cfg + 1u);
  }
  Descent<std::uint16_t> build_repeat(unsigned cfg, ImageId e);
  Descent<std::uint16_t> build_create(unsigned cfg, ImageId e);
  Descent<std::uint16_t> build_nest(unsigned cfg, ImageId e);
  Descent<std::uint16_t> build_closed(unsigned cfg, ImageId e);
  Descent<std::uint16_t> build_write(unsigned cfg, ImageId e);
  Descent<std::uint16_t> build_flag(unsigned cfg, ImageId e);
  std::uint16_t build_condition(unsigned cfg, ImageId e);
  Descent<std::uint16_t> build_implied(unsigned cfg, ImageId e);

  void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                  std::uint8_t tail) override;
  void emit_climb(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void emit_op(unsigned cfg, ImageId op, const void* opRow, std::uint8_t prec,
               Association group);
  std::uint16_t climb_chain(const void* row);
  void note_nests();

  std::unordered_map<std::uint64_t, ImageId> _endedIn;
};

}  // namespace cursiv::engine
