#pragma once

/// Strict and failure-tracked parse contexts used while building the CST.
/// The bounded-edit recovery context (`RecoveryContext`) lives in
/// RecoveryContext.hpp, downstream of this header: its out-of-line
/// `TrackedParseContext::skip`/`leaf` bodies downcast to it, so they must sit
/// after its complete type is visible.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/parser/ChoiceAttempt.hpp>
#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/LiteralFuzzyMatcher.hpp>
#include <pegium/core/parser/ParseDiagnostics.hpp>
#include <pegium/core/parser/RecoveryAnalysis.hpp>
#include <pegium/core/parser/RecoveryConstants.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/Skipper.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <utility>
#include <vector>

namespace pegium::parser {

struct ParseContext {
  struct Checkpoint {
    const char *cursor;
    const char *lastVisibleCursor;
    CstBuilder::Checkpoint builder;
  };

  const char *const begin;
  const char *const end;

  ParseContext(
      CstBuilder &builder, const Skipper &skipper,
      const utils::CancellationToken &cancelToken =
          utils::default_cancel_token)
      noexcept
      : begin(builder.input_begin()), end(builder.input_end()), _cursor(begin),
        _lastVisibleCursor(begin), _builder(builder), _skipper(&skipper),
        _cancelToken(cancelToken) {
  }

  [[nodiscard]] inline Checkpoint mark() const noexcept {
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextMark);

    return {.cursor = _cursor,
            .lastVisibleCursor = _lastVisibleCursor,
            .builder = _builder.mark()};
  }

  inline void rewind(const Checkpoint &checkpoint) noexcept {
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextRewind);
    _builder.rewind(checkpoint.builder);
    _cursor = checkpoint.cursor;
    _lastVisibleCursor = checkpoint.lastVisibleCursor;
  }

  using SkipperGuard = detail::SkipperGuard<ParseContext>;

  [[nodiscard]] SkipperGuard
  with_skipper(const Skipper &overrideSkipper) noexcept {
    return SkipperGuard{*this, _skipper, overrideSkipper};
  }

  [[nodiscard]] inline const char *enter() {
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextEnter);
    _builder.enter();

    return _cursor;
  }

  inline void exit(const char *checkpoint,
                   const grammar::AbstractElement *element) {
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextExit);
    // A nullable element entered after trailing trivia captures a checkpoint
    // past the last visible cursor, so clamp the end up to the begin: the node
    // becomes an empty span at the entry offset rather than an inverted one that
    // underflows CstNodeView::getText() into an out-of-bounds string_view.
    const auto beginOffset = static_cast<TextOffset>(checkpoint - begin);
    _builder.exit(beginOffset,
                  std::max(beginOffset, lastVisibleCursorOffset()), element);
    // Cancellation is checked at parser-rule granularity (ParserRule
    // calls `throw_if_cancelled(cancellationToken())` after each successful
    // rule exit), not on every Group/Repetition/Choice/Assignment
    // exit — the inner exits are too hot for a memory-load + branch on
    // every call (about 1.6% of strict-path Ir on the bench).
  }

  [[nodiscard]] inline NodeCount node_count() const noexcept {
    return _builder.node_count();
  }

  inline void
  override_grammar_element(NodeId id,
                           const grammar::AbstractElement *element) noexcept {
    _builder.override_grammar_element(id, element);
  }

  [[nodiscard]] constexpr const char *cursor() const noexcept {
    return _cursor;
  }
  [[nodiscard]] constexpr TextOffset cursorOffset() const noexcept {
    return static_cast<TextOffset>(_cursor - begin);
  }

  [[nodiscard]] constexpr TextOffset lastVisibleCursorOffset() const noexcept {
    return static_cast<TextOffset>(_lastVisibleCursor - begin);
  }

  /// Strict-only `ParseContext` does not track a max cursor distinct from
  /// the live cursor — backtracking does not produce observable failure
  /// state on this layer. Returns the live cursor so external callers
  /// (tests and recovery summaries that may receive either context type)
  /// see a coherent value. The real tracking lives in
  /// `TrackedParseContext`, which shadows this accessor.
  [[nodiscard]] constexpr const char *maxCursor() const noexcept {
    return _cursor;
  }

  [[nodiscard]] constexpr TextOffset maxCursorOffset() const noexcept {
    return static_cast<TextOffset>(_cursor - begin);
  }

  [[nodiscard]] constexpr const utils::CancellationToken &
  cancellationToken() const noexcept {
    return _cancelToken;
  }
  inline void skip() noexcept {
    _cursor = _skipper->skip(_cursor, _builder);
  }

  inline void leaf(const char *endPtr, const grammar::AbstractElement *element,
                   bool hidden = false, bool recovered = false) {
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextLeaf);
    const auto beginOffset = cursorOffset();
    const auto endOffset = static_cast<TextOffset>(endPtr - begin);
    _builder.leaf(beginOffset, endOffset, element, hidden, recovered);
    _cursor = endPtr;
    if (!hidden) [[likely]] {
      _lastVisibleCursor = endPtr;
    }
  }

  inline void leaf(const char *beginPtr, const char *endPtr,
                   const grammar::AbstractElement *element, bool hidden,
                   bool recovered) {
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ParseContextLeaf);
    const auto beginOffset = static_cast<TextOffset>(beginPtr - begin);
    const auto endOffset = static_cast<TextOffset>(endPtr - begin);
    _builder.leaf(beginOffset, endOffset, element, hidden, recovered);
    _cursor = endPtr;
    if (!hidden) [[likely]] {
      _lastVisibleCursor = endPtr;
    }
  }
  protected:
  const char *_cursor;
  const char *_lastVisibleCursor;
  CstBuilder &_builder;
  const Skipper *_skipper;
  const utils::CancellationToken &_cancelToken;
};

struct RecoveryContext;

[[nodiscard]] constexpr bool elements_equivalent_for_replay(
    const grammar::AbstractElement *lhs,
    const grammar::AbstractElement *rhs) noexcept {
  if (lhs == rhs) {
    return true;
  }
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  if (lhs->getKind() != rhs->getKind()) {
    return false;
  }
  if (lhs->getKind() == grammar::ElementKind::Literal) {
    const auto *lhsLit = static_cast<const grammar::Literal *>(lhs);
    const auto *rhsLit = static_cast<const grammar::Literal *>(rhs);
    return lhsLit->getValue() == rhsLit->getValue() &&
           lhsLit->isCaseSensitive() == rhsLit->isCaseSensitive();
  }
  return false;
}

// Strict parse context with failure-history tracking.
//
// This is not the nominal hot-path context: plain strict parses use
// `ParseContext`. `TrackedParseContext` exists for two internal cases only:
// - strict failure analysis with a `FailureHistoryRecorder`
// - the base class of `RecoveryContext`
struct TrackedParseContext : ParseContext {
  struct Checkpoint {
    ParseContext::Checkpoint parseCheckpoint;
    std::size_t failureHistorySize;
    bool recordFailureHistory;
  };

  TrackedParseContext(
      CstBuilder &builder, const Skipper &skipper,
      detail::FailureHistoryRecorder &failureRecorder,
      const utils::CancellationToken &cancelToken =
          utils::default_cancel_token) noexcept
      : ParseContext(builder, skipper, cancelToken), _maxCursor(begin),
        _failureRecorder(failureRecorder) {}

  [[nodiscard]] constexpr const char *maxCursor() const noexcept {
    return _maxCursor;
  }

  constexpr void restoreMaxCursor(const char *cursor) noexcept {
    _maxCursor = cursor;
  }

  /// Monotonically raise `_maxCursor` to replay the cumulative side effect
  /// of a memoized exploration that was skipped on cache hit.
  constexpr void bumpMaxCursor(const char *cursor) noexcept {
    if (cursor > _maxCursor) {
      _maxCursor = cursor;
    }
  }

  [[nodiscard]] constexpr TextOffset maxCursorOffset() const noexcept {
    return static_cast<TextOffset>(_maxCursor - begin);
  }

  [[nodiscard]] inline Checkpoint mark() const noexcept {
    return {.parseCheckpoint = ParseContext::mark(),
            .failureHistorySize =
                _recordFailureHistory ? _failureRecorder.mark() : 0u,
            .recordFailureHistory = _recordFailureHistory};
  }

  using ParseContext::rewind;

  inline void rewind(const Checkpoint &checkpoint) noexcept {
    ParseContext::rewind(checkpoint.parseCheckpoint);
    _recordFailureHistory = checkpoint.recordFailureHistory;
    if (_recordFailureHistory) [[likely]] {
      _failureRecorder.rewind(checkpoint.failureHistorySize);
    }
  }

  void skip() noexcept;

  void leaf(const char *endPtr, const grammar::AbstractElement *element,
            bool hidden = false, bool recovered = false);
  void leaf(const char *beginPtr, const char *endPtr,
            const grammar::AbstractElement *element,
            bool hidden = false, bool recovered = false);

  [[nodiscard]] SkipperGuard
  with_skipper(const Skipper &overrideSkipper) noexcept {
    // Invalidate the no-builder skip cache: a different skipper can map the
    // same cursor to a different end.
    _skipCacheBegin = nullptr;
    return SkipperGuard{*this, _skipper, overrideSkipper};
  }

  [[nodiscard]] const char *skip_without_builder(const char *from) const noexcept {
    // 1-entry cache: the no-builder skipper is purely a function of (begin,
    // _skipper) and is called heavily during recovery exploration at
    // overlapping positions. Caching the most recent (begin -> end) result
    // avoids re-walking the same trivia run dozens of times per offset.
    if (from == _skipCacheBegin && _skipper == _skipCacheSkipper) {
      return _skipCacheEnd;
    }
    const char *const to = _skipper->skip(from);
    _skipCacheBegin = from;
    _skipCacheEnd = to;
    _skipCacheSkipper = _skipper;
    return to;
  }

  [[nodiscard]] constexpr bool isFailureHistoryRecordingEnabled() const noexcept {
    return _recordFailureHistory;
  }

  [[nodiscard]] std::size_t failureHistorySize() const noexcept {
    return _failureRecorder.mark();
  }

  [[nodiscard]] std::size_t furthestFailureHistorySize() const noexcept {
    return _failureRecorder.furthestVisibleLeafCount();
  }

  void bumpFurthestFailureHistorySize(std::size_t value) noexcept {
    _failureRecorder.bumpFurthestVisibleLeafCount(value);
  }

  void bumpFurthestFailureOffset(TextOffset value) noexcept {
    _failureRecorder.bumpFurthestOffset(value);
  }

  [[nodiscard]] TextOffset furthestFailureOffset() const noexcept {
    return _failureRecorder.furthestOffset();
  }

protected:
  /// Highest cursor position reached during parsing, including failed
  /// alternatives that were rewound. Recovery uses this to position the
  /// failure window. Strict-only `ParseContext` does not track it; this
  /// state lives at the `TrackedParseContext` layer where recovery becomes
  /// observable.
  const char *_maxCursor;
  detail::FailureHistoryRecorder &_failureRecorder;
  // 1-entry no-builder skip cache. `mutable` so const callers can populate
  // it without compromising the logical const-ness of `skip_without_builder`.
  // Strict-only `ParseContext` does not need a no-builder skipper (no recovery
  // probes call into it), so the cache and its companion `skip_without_builder`
  // live here.
  mutable const char *_skipCacheBegin = nullptr;
  mutable const char *_skipCacheEnd = nullptr;
  // The cached (begin -> end) is only valid for the skipper that produced it: a
  // local-skipper scope (with_skipper) can map the same cursor to a different
  // end, so the active skipper is part of the cache key.
  mutable const Skipper *_skipCacheSkipper = nullptr;
  bool _recordFailureHistory = true;
  bool _runRecoveryBookkeeping = false;
};


} // namespace pegium::parser
