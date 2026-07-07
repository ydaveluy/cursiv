#pragma once

/// Strict-failure analysis helpers used to seed global recovery.

#include <cstdint>
#include <memory>
#include <vector>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/ParserRule.hpp>
#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/Skipper.hpp>
#include <pegium/core/syntax-tree/RootCstNode.hpp>
#include <pegium/core/text/TextSnapshot.hpp>
#include <pegium/core/utils/Cancellation.hpp>

namespace pegium::parser::detail {

struct StrictParseSummary {
  TextOffset inputSize = 0;
  TextOffset parsedLength = 0;
  TextOffset lastVisibleCursorOffset = 0;
  TextOffset maxCursorOffset = 0;
  bool entryRuleMatched = false;
  bool fullMatch = false;
};

struct StrictParseResult {
  StrictParseSummary summary;
  std::unique_ptr<RootCstNode> cst;
};

struct FailureLeaf {
  TextOffset beginOffset = 0;
  TextOffset endOffset = 0;
  const grammar::AbstractElement *element = nullptr;
};

struct FailureSnapshot {
  TextOffset maxCursorOffset = 0;
  std::vector<FailureLeaf> failureLeafHistory;
  std::size_t failureTokenIndex = 0;
  bool hasFailureToken = false;
};

struct StrictFailureEngineResult {
  StrictParseResult strictResult;
  FailureSnapshot snapshot;
};

struct RecoveryWindow {
  TextOffset beginOffset = 0;
  TextOffset editFloorOffset = 0;
  TextOffset maxCursorOffset = 0;
  std::uint32_t tokenCount = 0;
  std::uint32_t forwardTokenCount = 0;
  std::uint32_t visibleLeafBeginIndex = 0;
  TextOffset stablePrefixOffset = 0;
  bool hasStablePrefix = false;
};

class FailureHistoryRecorder {
public:
  using Checkpoint = std::size_t;

  explicit FailureHistoryRecorder(const char *inputBegin) noexcept
      : _inputBegin(inputBegin) {}

  [[nodiscard]] Checkpoint mark() const noexcept { return _currentVisibleLeafCount; }
  [[nodiscard]] TextOffset furthestOffset() const noexcept {
    return _furthestOffset;
  }
  [[nodiscard]] std::size_t furthestVisibleLeafCount() const noexcept {
    return _furthestVisibleLeafCount;
  }

  /// Monotonically raise `_furthestVisibleLeafCount` to replay the cumulative
  /// side effect of a memoized exploration that was skipped on cache hit.
  void bumpFurthestVisibleLeafCount(std::size_t value) noexcept {
    if (value > _furthestVisibleLeafCount) {
      _furthestVisibleLeafCount = value;
    }
  }

  /// Monotonically raise `_furthestOffset` to replay the cumulative side
  /// effect of a memoized exploration that was skipped on cache hit.
  void bumpFurthestOffset(TextOffset value) noexcept {
    if (value > _furthestOffset) {
      _furthestOffset = value;
    }
  }

  inline void rewind(Checkpoint checkpoint) noexcept {
    _currentVisibleLeafCount = checkpoint;
  }

  inline void onLeaf(const char *begin, const char *end,
                     const grammar::AbstractElement *element,
                     bool hidden) noexcept {
    if (hidden || begin == nullptr || end == nullptr || end <= begin) {
      return;
    }
    const FailureLeaf leaf{
        .beginOffset = static_cast<TextOffset>(begin - _inputBegin),
        .endOffset = static_cast<TextOffset>(end - _inputBegin),
        .element = element,
    };
    if (_currentVisibleLeafCount < _visibleLeaves.size()) {
      _visibleLeaves[_currentVisibleLeafCount] = leaf;
    } else {
      _visibleLeaves.push_back(leaf);
    }
    ++_currentVisibleLeafCount;
    updateFurthest(end);
  }

  inline void onCursor(const char *cursor) noexcept { updateFurthest(cursor); }

  [[nodiscard]] FailureSnapshot snapshot(TextOffset maxCursorOffset) const;

private:
  inline void updateFurthest(const char *cursor) noexcept {
    const auto offset = static_cast<TextOffset>(cursor - _inputBegin);
    if (offset < _furthestOffset) {
      return;
    }
    _furthestOffset = offset;
    _furthestVisibleLeafCount = _currentVisibleLeafCount;
  }

  const char *_inputBegin = nullptr;
  TextOffset _furthestOffset = 0;
  std::size_t _currentVisibleLeafCount = 0;
  std::size_t _furthestVisibleLeafCount = 0;
  std::vector<FailureLeaf> _visibleLeaves;
};

[[nodiscard]] StrictParseResult
run_strict_parse(const grammar::ParserRule &entryRule, const Skipper &skipper,
                 const text::TextSnapshot &text,
                 const utils::CancellationToken &cancelToken = {},
                 FailureHistoryRecorder *failureRecorder = nullptr);

[[nodiscard]] StrictFailureEngineResult
run_strict_parse_with_failure_snapshot(
    const grammar::ParserRule &entryRule, const Skipper &skipper,
    const text::TextSnapshot &text,
    const utils::CancellationToken &cancelToken = {});

[[nodiscard]] FailureSnapshot
snapshot_from_committed_cst(const RootCstNode &cst,
                            TextOffset maxCursorOffset) noexcept;

[[nodiscard]] bool
should_fallback_to_parsed_length_snapshot(const FailureSnapshot &snapshot,
                                          TextOffset parsedLength) noexcept;

void finalize_failure_token_index(FailureSnapshot &snapshot) noexcept;

[[nodiscard]] RecoveryWindow
compute_recovery_window(const FailureSnapshot &snapshot,
                        std::uint32_t backwardTokenCount,
                        std::uint32_t forwardTokenCount,
                        TextOffset stablePrefixFloorOffset) noexcept;

[[nodiscard]] RecoveryWindow
compute_recovery_window(const FailureSnapshot &snapshot,
                        std::uint32_t tokenCount) noexcept;

/// True iff `entryRule`'s leading visible terminal is reachable without
/// crossing an `AndPredicate` / `NotPredicate` guard. Recovery uses this
/// to decide whether a "delete one codepoint at offset 0 then retry the
/// entry rule" probe is admissible: a predicate-guarded leading entry
/// would observe the deleted prefix and reject, so the retry is only
/// useful when the leading entry is unguarded. The function is purely
/// grammar-structural and has no recovery state — it lives here next to
/// the other failure-analysis helpers rather than in `RecoverySearch`.
///
/// Returns false for non-`ParserRule` entries (the grammar shape on
/// which this property is defined).
[[nodiscard]] bool entry_rule_has_unguarded_leading_visible_entry(
    const grammar::ParserRule &entryRule) noexcept;

/// Derives the recovery word-character mask from the grammar reachable from
/// `entryRule`: the union of every `CharacterRange` alphabet that repeats
/// inside a terminal rule's body — the CONTINUATION alphabet, which is what
/// a boundary check asks ("does the next codepoint continue a token"), not
/// the start alphabet. Predicate subtrees (`!` / `&`) are pruned wholesale:
/// a class reachable only through a predicate guards a boundary, it does not
/// extend the token. Rule references reset the repetition context (repeating
/// a RULE does not repeat the referenced rule's characters). When the
/// grammar declares no repeated in-terminal class, falls back to the default
/// (C-family) mask, making the derivation strictly additive; codepoints
/// >= 0x80 stay word-like regardless via `is_word_char_at`'s passthrough.
[[nodiscard]] WordCharMask
derive_word_char_mask(const grammar::ParserRule &entryRule);

/// The word-character mask to install on a recovery/expect context for
/// `entryRule`: the table cached on the rule by the reflection bootstrap
/// when available (registered languages — no per-parse work, no shared
/// mutation), otherwise derived on the spot (standalone parses without a
/// service registration; a read-only grammar walk, safe under concurrent
/// parses).
[[nodiscard]] inline WordCharMask
word_char_mask_for(const grammar::ParserRule &entryRule) {
  if (const auto *table = entryRule.recoveryWordCharTable();
      table != nullptr) {
    WordCharMask mask;
    mask.table = *table;
    return mask;
  }
  return derive_word_char_mask(entryRule);
}

} // namespace pegium::parser::detail
