#pragma once

/// Non-template recovery engine for the InfixRule editable tail.
///
/// The Pratt tail's recovery machinery (observe the RHS primary, enumerate
/// the ContinueTail / DeleteOperatorNoise candidates, select by the shared
/// recovery key, replay the winner, and the stray-operator-run /
/// RHS-prefix-noise delete scans) never reads the (T, Element, Operators...)
/// template parameters directly: it reaches the operators and the primary
/// only through parse/probe entry points. This engine hosts that machinery
/// once, compiled in InfixTailRecoveryEngine.cpp; each InfixRule Model
/// materializes a small `InfixTailOps` table of type-erased trampolines.
///
/// The strict and expect tails, and the operator-pack matchers (which carry
/// compile-time precedences and the operator-shadowing checks), stay
/// templated in InfixRule.hpp.

#include <cstdint>

#include <pegium/core/grammar/InfixRule.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>

namespace pegium::parser::detail {

/// Closed list of `Infix` recovery candidate families. Private to the infix
/// tail engine — no other combinator references this vocabulary.
///
/// Strict-path discipline: recovery-side; none of these types are
/// constructed on the strict-only nominal path.
enum class InfixCandidateFamily : std::uint8_t {
  /// The tail continues: a strict operator at the cursor followed
  /// by RHS strict or recoverable acceptance.
  ContinueTail,
  /// The tail recovers from operator-position noise: a bounded
  /// delete of pre-operator junk, after which a strict RHS primary
  /// starts.
  DeleteOperatorNoise,
};

/// Type-erased view of one InfixRule Model: the operator-pack matchers and
/// the primary's parse/probe entry points, plus the two compile-time scalars
/// the tail loop consumes. Built on the stack at each recovery descent.
struct InfixTailOps {
  const void *model = nullptr;
  /// The grammar::InfixRule passed to ctx.exit when a tail level completes.
  const grammar::InfixRule *owner = nullptr;
  /// Highest operator precedence (== sizeof...(Operators)); the tail loop
  /// terminates when minPrecedence exceeds it.
  std::int32_t maxPrecedence = 0;

  bool (*operatorFastProbe)(const void *model, RecoveryContext &ctx,
                            std::int32_t minPrecedence) = nullptr;
  bool (*matchOperatorEditable)(const void *model, RecoveryContext &ctx,
                                std::int32_t minPrecedence,
                                std::int32_t &nextMinPrecedence) = nullptr;
  bool (*parsePrimary)(const void *model, RecoveryContext &ctx) = nullptr;
  bool (*parsePrimaryNoEdits)(const void *model,
                              RecoveryContext &ctx) = nullptr;
  bool (*fastProbePrimary)(const void *model, RecoveryContext &ctx) = nullptr;
  bool (*probeLocallyRecoverablePrimary)(const void *model,
                                         RecoveryContext &ctx) = nullptr;
};

struct InfixTailReplayResult {
  bool matched = true;
  // Only the stray-operator-run recovery ever sets this true (to halt the
  // tail loop once the repair has consumed the rest of the editable
  // expression); every other replay path leaves it false or forwards a
  // nested result unchanged.
  bool stopTail = false;
};

/// Editable-mode Pratt tail at `minPrecedence`, recursing on itself for
/// nested precedence levels. Mirrors the strict tail's operator loop with
/// the candidate-based RHS recovery.
[[nodiscard]] InfixTailReplayResult
infix_parse_tail_editable(RecoveryContext &ctx, const InfixTailOps &ops,
                          std::int32_t minPrecedence, const char *lhsStart);

} // namespace pegium::parser::detail
