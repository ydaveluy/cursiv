// A state with several actions for its token: each is read ahead, on a
// reading of its own that writes no tree, until one reading is left. Where
// readings meet they go on as one; the tokens they read, then the grammar's
// order, elect among what they derived.
#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/engine/parser/Explorer.hpp"
#include "cursiv/utils/FunctionRef.hpp"

namespace cursiv::engine {

// The actions a split reads on with: of the `count` from `first`, its
// ordinary ones where one `takes` the token in hand, else the last
// `rereads`, its lexical fallbacks.
struct Followed {
  std::uint32_t begin, end;
  bool fallback;
};
constexpr Followed followed(std::uint32_t first, std::uint32_t count, std::uint32_t rereads,
                            bool takes) {
  const std::uint32_t fallbacks = first + count - rereads;
  return takes || rereads == 0 ? Followed{first, fallbacks, false}
                               : Followed{fallbacks, first + count, true};
}

// Readings read depth first, the one opened last first, each with its
// context. `first` tells a split met again at its place under the same key,
// which opens nothing.
template <class Context>
struct Walk {
  struct Open {
    Reading reading;
    Context context;
  };
  std::vector<Open> open;

  void clear() {
    open.clear();
    _seen.clear();
  }
  // Whether split `x` is met at its place under `key` for the first time.
  bool first(const Explorer& x, std::uint64_t key = 0) {
    return _seen.emplace(x.site(x.arms), key).second;
  }
  // Opens the actions of a split `at` stands at from `arms.begin` to
  // `arms.end`, the first to be read first.
  void branch(const Reading& at, Followed arms, const Context& context) {
    Reading r = at;
    for (std::uint32_t arm = arms.end; arm-- > arms.begin;) {
      r.resume = arm;
      open.push_back({r, context});
    }
  }

 private:
  std::set<std::pair<SplitSite, std::uint64_t>> _seen;
};

// Reductions whose continuation is already being explored.
struct Explorer::Closure {
  struct Step {
    std::uint32_t node, fresh, production, guards;
    TextOffset at, end;
    std::uint64_t token, context;
    Indented indent;
    bool operator==(const Step&) const = default;
    struct Hash {
      std::size_t operator()(const Step& s) const {
        return (std::uint64_t{s.node} * 0x9E3779B97F4A7C15ull) ^ s.token ^
               (std::uint64_t{s.production} << 32) ^ s.at ^ (std::uint64_t{s.fresh} << 48) ^
               s.indent.level ^ s.indent.bracket ^ s.end ^ s.context ^
               (std::uint64_t{s.guards} * 0xC2B2AE3D27D4EB4Full);
      }
    };
  };
  const std::uint32_t* guards = nullptr;
  std::unordered_set<Step, Step::Hash> seen{};
  std::uint64_t context = 0;
};

// Reads ahead from a state that has the `count` actions from `first` on for
// `tok`, on top of `stack` and the states `above` it, the text read up to
// `at` at indentation `indented`. The last `rereads` actions are lexical
// fallbacks. `plan` gains the action of each split the elected reading met,
// in the order it met them: the first is this split's. Where the text refuses
// every reading, they stop at the split where the furthest parts from one a
// repair may still take.
void resolve(Machine& text, std::span<const Frame> stack, std::span<const std::uint32_t> above,
             const char* at, Token tok, Indented indented, std::uint32_t first,
             std::uint32_t count, std::uint32_t rereads, std::vector<std::uint32_t>& plan);

// After any shared prefix, the arm whose stack alone shifts the token it reads.
Prediction predict_prefix(Machine& m, std::span<const PrefixArm> arms, const PrefixPath* path = nullptr);

// The arm the proved prefixes elect for the cell the machine stopped at.
Prediction predict_cell(Machine& m, const PrefixTable& table);

// Whether an arm takes the held token or, given `terminal`, whether a reading
// shifts a token of it there, the longest of which `shifted` then holds.
bool takes(Machine& text, Cactus& stacks, const Reading& from,
           std::uint32_t first, std::uint32_t count, std::uint16_t terminal = kNoToken, Token* shifted = nullptr);

// The actions a split of `from` reads on with, its ordinary ones asked of
// `takes`.
inline Followed followed(Machine& text, Cactus& stacks, const Reading& from, std::uint32_t first,
                         std::uint32_t count, std::uint32_t rereads) {
  return followed(first, count, rereads,
                  rereads == 0 || takes(text, stacks, from, first, count - rereads));
}

// The action the machine goes on from where a split stopped it: the next of
// its plan, the text read ahead where it has none for this split; none where
// the text refuses every reading. A caller requesting a prediction receives its evidence.
std::uint32_t arm(Machine& m, Prediction* prediction = nullptr);

// Runs to the next stop, a split where `stops` holds included. With `retry`,
// permits predictions and reports whether recovery must restart from the full
// elections.
Exit run(Machine& m, bool* retry = nullptr, FunctionRef<bool()> stops = [] { return false; });

}  // namespace cursiv::engine
