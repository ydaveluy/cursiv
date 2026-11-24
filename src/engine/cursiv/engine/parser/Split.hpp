// A state with several actions for its token: each is read ahead, on a
// reading of its own that writes no tree, until one reading is left. Where
// readings meet they go on as one, and the grammar's order elects among
// what they derived.
#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <unordered_set>
#include <vector>

#include "cursiv/engine/parser/Explorer.hpp"

namespace cursiv::engine {

// Explore each branch of a lexical fallback until it leaves the split or
// returns to the same stack and token without advancing.
template<class R, class Context>
struct Forks {
  struct Entry { R reading; Context context{}; bool leave = false; };
  std::vector<Entry> open;
  std::set<SplitSite> active;

  bool enter(R r, std::uint32_t first) {
    if (!active.insert(r.site(first)).second) return false;
    r.resume = first;
    open.push_back({r, {}, true});
    return true;
  }

  bool pending() {
    while (!open.empty() && open.back().leave) {
      const auto& r = open.back().reading;
      active.erase(r.site(r.resume));
      open.pop_back();
    }
    return !open.empty();
  }
};

// Empty reductions whose continuation is already being explored.
struct Explorer::Closure {
  struct EmptyStep {
    std::uint32_t node, fresh, production, guards;
    TextOffset at, end;
    std::uint64_t token, context;
    Indented indent;
    bool operator==(const EmptyStep&) const = default;
    struct Hash {
      std::size_t operator()(const EmptyStep& s) const {
        return (std::uint64_t{s.node} * 0x9E3779B97F4A7C15ull) ^ s.token ^
               (std::uint64_t{s.production} << 32) ^ s.at ^ (std::uint64_t{s.fresh} << 48) ^
               s.indent.level ^ s.indent.depth ^ s.end ^ s.context ^
               (std::uint64_t{s.guards} * 0xC2B2AE3D27D4EB4Full);
      }
    };
  };
  const std::uint32_t* guards = nullptr;
  std::unordered_set<EmptyStep, EmptyStep::Hash> seen{};
  std::uint64_t context = 0;
};

// Reads ahead from a state that has the `count` actions from `first` on for
// `tok`, on top of `stack`, the text read up to `at` at indentation
// `indented`. The last `rereads` actions are lexical fallbacks. `plan` gains
// the action of each split the elected reading met, in the order it met them:
// the first is this split's. Where the text refuses every reading, they stop
// at the split where the furthest parts from one a repair may still take.
void resolve(Machine& text, std::span<const Frame> stack, const char* at, Token tok,
                      Indented indented, std::uint32_t first, std::uint32_t count,
                      std::uint32_t rereads, std::vector<std::uint32_t>& plan);

// After any shared prefix, the arm whose stack alone shifts the longest token.
Prediction predict_prefix(Machine& m, std::span<const PrefixArm> arms, const PrefixPath* path = nullptr);

// The arm the proved prefixes elect for the cell the machine stopped at.
Prediction predict_cell(Machine& m, const PrefixTable& table);

// Whether an arm takes the held token, or elects a shift of `terminal` in `shifted`.
bool takes(Machine& text, Cactus& stacks, const Reading& from,
           std::uint32_t first, std::uint32_t count, std::uint16_t terminal = kNoToken, Token* shifted = nullptr);

// The action the machine goes on from where a split stopped it: the next of
// its plan, the text read ahead where it has none for this split; none where
// the text refuses every reading. A caller requesting a prediction receives its evidence.
std::uint32_t arm(Machine& m, Prediction* prediction = nullptr);

// Runs to the next stop. With `retry`, permits predictions and reports whether
// recovery must restart from the full elections.
Exit run(Machine& m, bool* retry = nullptr);

}  // namespace cursiv::engine
