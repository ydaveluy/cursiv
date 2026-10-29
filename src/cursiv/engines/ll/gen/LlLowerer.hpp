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

struct LlLowerState {
  LlLowered lowered{};
  detail::llow::Pool pool{};
};

class LlLowerer : private LlLowerState, public Lowerer {
 public:
  LlLowerer(GrammarImage& image, const LowerDesc& desc);

  LlLowered run();

 private:
  using Shape = detail::llow::Shape;
  using Node = detail::llow::Node;

  std::uint16_t intern_name(std::string_view text) override;
  ImageId ended(ImageId id) override;
  void declare_start() override;
  void declare_rules() override;

  ImageId ended_in(ImageId id, bool inPredicate);
  ImageId operator_body(unsigned cfg, ImageId op,
                        std::vector<std::uint32_t>& reading);

  std::uint16_t lay(unsigned cfg, ImageId e);
  std::uint16_t lay_ref(unsigned cfg, std::uint32_t rule);
  std::uint16_t lay_embed(unsigned cfg, ImageId e);
  std::uint16_t lay_seq(unsigned cfg, ImageId e);
  std::uint16_t lay_kids(unsigned cfg, Shape kind,
                         const std::vector<ImageId>& es);
  std::uint16_t gather(unsigned cfg, Shape kind,
                       const std::vector<std::uint16_t>& kids);
  std::uint16_t lay_repeat(unsigned cfg, ImageId e);
  std::uint16_t lay_create(unsigned cfg, ImageId e);
  std::uint16_t lay_nest(unsigned cfg, ImageId e);
  std::uint16_t lay_closed(unsigned cfg, ImageId e);
  std::uint16_t lay_write(unsigned cfg, ImageId e);
  std::uint16_t lay_flag(unsigned cfg, ImageId e);
  std::uint16_t lay_condition(unsigned cfg, ImageId e);
  std::uint16_t lay_implied(unsigned cfg, ImageId e);
  std::uint16_t mark(unsigned cfg, std::uint16_t at);
  std::uint16_t wrap(unsigned cfg, std::uint16_t at);
  std::pair<std::uint16_t, std::uint16_t> write_site(unsigned cfg,
                                                     std::uint16_t at);

  void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                  std::uint8_t tail) override;
  void emit_climb(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void emit_op(unsigned cfg, ImageId op, const void* opRow, std::uint8_t prec,
               Association group);
  std::uint16_t climb_chain(const void* row);
  void note_nests();

  LlLowered& _o;
  detail::llow::Pool& _p;
  LlLowCfg& _cfg;
  std::unordered_map<std::uint64_t, ImageId> _endedIn;
};

}  // namespace cursiv
