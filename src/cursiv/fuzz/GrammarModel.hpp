// The grammar as declared, with the engine erased: the generator's sole
// subject. Not `cursiv::Grammar`, which answers for the engine's own
// tables -- reading those here would make the generator agree with the
// engine by construction and prove nothing.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/fuzz/ElementKind.hpp"

namespace cursiv::fuzzer {

// A repetition with no upper bound.
inline constexpr std::uint16_t kModelUnbounded = 0xFFFFu;

struct GrammarModel {
  constexpr virtual ~GrammarModel() = default;

  // How many positions, and where a document starts.
  [[nodiscard]] virtual ElementId positionCount() const noexcept = 0;
  [[nodiscard]] virtual ElementId entryRule() const noexcept = 0;

  [[nodiscard]] virtual ElementKind kindOf(ElementId) const noexcept = 0;
  // What this position contains: a rule's body, a group's members. Empty
  // for a leaf. The graph has cycles -- a rule reaching itself through its
  // body -- so a reader descends with its own guard.
  [[nodiscard]] virtual std::span<const ElementId> childrenOf(
      ElementId) const noexcept = 0;
  // The bytes a literal demands of the document; empty for every other kind.
  [[nodiscard]] virtual std::string_view textOf(ElementId) const noexcept = 0;
  // Set only for a named terminal; every other kind is named by its spelling.
  [[nodiscard]] virtual std::string_view nameOf(ElementId) const noexcept = 0;
  // The member a directive writes, by name; empty where it writes none.
  [[nodiscard]] virtual std::string_view featureOf(
      ElementId) const noexcept = 0;
  // How many times, where the kind is `repetition`.
  [[nodiscard]] virtual std::uint16_t minOf(ElementId) const noexcept = 0;
  [[nodiscard]] virtual std::uint16_t maxOf(ElementId) const noexcept = 0;
};

}  // namespace cursiv::fuzzer
