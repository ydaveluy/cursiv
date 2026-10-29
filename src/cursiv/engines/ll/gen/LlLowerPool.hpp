// The shapes the LL lowering lays a body out as, and the emission that turns
// them into the program's nodes: a production is decided over the whole body,
// since its optional positions distribute over one another.
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/engines/ll/gen/LlLowered.hpp"
#include "cursiv/engines/shared/gen/LowerPool.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"

namespace cursiv {

class LlRecordPlan;

namespace detail::llow {

// `end` is end-of-input at a position (zero-width, no slot); `none` takes no
// slot and says nothing.
enum class Shape : std::uint8_t {
  sym,
  seq,
  alt,
  unordered,
  rep,
  none,
  end,
  condition,
  implied
};

// A shape limit is a refusal, never a truncation. The refusal is recorded in
// `cfg.fault` and the walk carries on writing a well-formed value: the gate
// over the lowering states it once, naming the rule (`GrammarFault.hpp`).

// The actions one column of one element owes, innermost first.
using Acts = std::vector<std::uint16_t>;

[[noreturn]] inline void too_many_positions() {
  throw LowerRefused(LowerFail::tooManyPositions);
}

struct Node {
  Shape kind{};
  std::uint16_t sym = 0;    // sym: the symbol | seq,alt: kid base | rep: kid
  std::uint16_t count = 0;  // seq,alt: kid count
  std::uint32_t min = 0, max = 0;
  // The two runs a directive standing over this element owes: what it writes,
  // and the record it opens. A directive takes no position of its own -- it
  // says what the position under it does.
  Acts row{};
  Acts made{};
  // The skip configuration the element is read under: the region it stands
  // in, zero for the grammar's. The regime rides the element -- a rule whose
  // own body opens a region has an interior of its own and the caller's
  // entry, which no per-rule flag can say.
  std::uint8_t cfg = 0;
  // The section a call opens, its configuration plus one.
  std::uint8_t section = 0;
  bool foldStep = false;
  bool leftFold = false;
  bool layered = false;
  bool unorderedTurns = false;
  // unordered: the items the text may leave out, one bit each. Each stands in
  // its present form, and the group may end where only these are left.
  std::uint32_t unorderedOptional = 0;
};

// What the walk holds while it runs, and nothing longer: the elements a body
// was read into, and the two pools a reification interns through.
struct Pool : LowerPool {
  std::vector<Node> e{};
  std::vector<std::uint16_t> kid{};

  std::uint16_t push(Node x) {
    if (e.size() >= kNoRow) too_many_positions();
    e.push_back(std::move(x));
    return static_cast<std::uint16_t>(e.size() - 1);
  }
};

Acts cat(const Acts& a, const Acts& b);

// Position `node`, as the choice the parse makes there, minted where the
// shape holds one. What separates its arms is the construction's to settle;
// the lowering only says where a decision stands.
std::uint16_t with_decision(LlLowered& o, std::uint16_t node,
                            std::uint16_t nalts, std::uint16_t owner);

std::uint16_t put_node(LlLowered& o, const LlNode& n,
                       const LlNodeBuild& b = {});

std::uint16_t put_group(LlLowered& o, LlShape kind,
                        std::span<const std::uint16_t> ids, std::uint16_t row,
                        const LlNodeBuild& b);

// `made` and `row` are the whole chain standing over this position, the
// position's own included: a caller reading a kid takes the kid's before it
// descends. A choice never carries one -- a directive over it distributes,
// since a write on a step every arm shares could not say which arm the value
// came from.
std::uint16_t emit(LlLowered& o, const Pool& p, std::uint16_t at,
                   const Acts& made, const Acts& row, std::uint16_t owner);

// One climb operator per token of an atomic operator, `made` and `row` the
// chain over `at` as `emit` takes them. A condition reads the gap on its own
// side of the token: those in front of it stay in front, those behind it stay
// behind.
void emit_operator_nodes(LlLowered& o, const Pool& p, std::uint16_t at,
                         const Acts& made, const Acts& row, std::uint8_t prec,
                         Association group, std::uint8_t cfg,
                         std::vector<std::uint16_t> before = {},
                         std::vector<std::uint16_t> after = {});

void ride_chains_down(LlLowered& o, const LlRecordPlan& plan);
void lay_own_records(LlLowered& o, const LlRecordPlan& plan);

}  // namespace detail::llow
}  // namespace cursiv
