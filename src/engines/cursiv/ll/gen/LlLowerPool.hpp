// The shapes the LL lowering arranges a body into, and the emission that turns
// them into the program's nodes: a production is decided over the whole body,
// since its optional positions distribute over one another.
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/grammar/Association.hpp"
#include "cursiv/ll/gen/LlLowered.hpp"
#include "cursiv/shared/gen/LowerPool.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"

namespace cursiv::engine {

class LlRecordPlan;

namespace llow {

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
// `cfg.fault` and the walk carries on writing a well-formed value: the check
// after the lowering reports it once, naming the rule (`GrammarFault.hpp`).

// The actions one column of one element needs, innermost first.
using Acts = std::vector<std::uint16_t>;

[[noreturn]] inline void too_many_positions() {
  throw LowerRefused(LowerFail::tooManyPositions);
}

struct Node {
  Shape kind{};
  std::uint16_t sym = 0;  // sym: the symbol | seq,alt: child base | rep: child
  std::uint16_t count = 0;  // seq,alt: child count
  std::uint32_t min = 0, max = 0;
  // The two runs of a directive over this element: what it writes, and the
  // record it opens. A directive takes no position of its own; it says what
  // the position under it does.
  Acts row{};
  Acts made{};
  // The hover text of this use of a token.
  std::string_view doc{};
  // The skip configuration the element is read under: its region, zero for the
  // grammar's. A rule whose body opens a region has its own interior and the
  // caller's entry, so this belongs to the element.
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
  std::vector<std::uint16_t> child{};

  std::uint16_t push(Node x) {
    if (e.size() >= kNoRow) too_many_positions();
    e.push_back(std::move(x));
    return static_cast<std::uint16_t>(e.size() - 1);
  }
};

Acts cat(const Acts& a, const Acts& b);

// Position `node`, as the choice the parse makes there, created where the
// shape holds one. The construction decides what separates its arms; the
// lowering only says where a decision stands.
std::uint16_t with_decision(LlLowered& o, std::uint16_t node,
                            std::uint16_t nalts, std::uint16_t owner);

std::uint16_t put_node(LlLowered& o, const LlNode& n,
                       const LlNodeBuild& b = {});

std::uint16_t put_group(LlLowered& o, LlShape kind,
                        std::span<const std::uint16_t> ids, std::uint16_t row,
                        const LlNodeBuild& b);

// `made` and `row` are the whole chain over this position, its own included. A
// choice never carries one: a directive over it goes to each arm, so a write
// can tell which arm gave the value.
Descent<std::uint16_t> emit(LlLowered& o, const Pool& p, std::uint16_t at,
                            const Acts& made, const Acts& row,
                            std::uint16_t owner);

// One climb operator per token of an atomic operator, `made` and `row` as
// `emit` takes them. A condition stays on its own side of the token.
Descent<void> emit_operator_nodes(LlLowered& o, const Pool& p, std::uint16_t at,
                                  const Acts& made, const Acts& row,
                                  std::uint8_t prec, Association group,
                                  std::uint8_t cfg,
                                  std::vector<std::uint16_t> before = {},
                                  std::vector<std::uint16_t> after = {});

void move_chains_down(LlLowered& o, const LlRecordPlan& plan);
void add_own_records(LlLowered& o, const LlRecordPlan& plan);

}  // namespace llow
}  // namespace cursiv::engine
