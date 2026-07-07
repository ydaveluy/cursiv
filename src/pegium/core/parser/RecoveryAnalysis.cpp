#include <pegium/core/parser/RecoveryAnalysis.hpp>

#include <algorithm>
#include <utility>

#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/CharacterRange.hpp>
#include <pegium/core/grammar/DataTypeRule.hpp>
#include <pegium/core/grammar/Group.hpp>
#include <pegium/core/grammar/InfixRule.hpp>
#include <pegium/core/grammar/OrderedChoice.hpp>
#include <pegium/core/grammar/Repetition.hpp>
#include <pegium/core/grammar/TerminalRule.hpp>
#include <pegium/core/grammar/UnorderedGroup.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/syntax-tree/CstBuilder.hpp>

namespace pegium::parser::detail {

namespace {

template <typename ContextFactory>
[[nodiscard]] StrictParseResult
run_strict_parse_with_context(const grammar::ParserRule &entryRule,
                              const Skipper &skipper,
                              const text::TextSnapshot &text,
                              const utils::CancellationToken &cancelToken,
                              ContextFactory &&makeContext) {
  StrictParseResult result;
  result.summary.inputSize = static_cast<TextOffset>(text.size());

  auto cst = std::make_unique<RootCstNode>(text);
  CstBuilder builder(*cst);
  auto parseCtx = makeContext(builder, skipper, cancelToken);
  parseCtx.skip();
  const auto attemptCheckpoint = parseCtx.mark();

  result.summary.entryRuleMatched = entryRule.rule(parseCtx);
  const auto failureParsedLength = parseCtx.cursorOffset();
  const auto failureMaxCursorOffset = parseCtx.maxCursorOffset();
  if (!result.summary.entryRuleMatched) {
    parseCtx.rewind(attemptCheckpoint);
  }

  parseCtx.skip();
  result.summary.parsedLength = result.summary.entryRuleMatched
                                    ? parseCtx.cursorOffset()
                                    : failureParsedLength;
  result.summary.lastVisibleCursorOffset = parseCtx.lastVisibleCursorOffset();
  result.summary.maxCursorOffset = result.summary.entryRuleMatched
                                       ? parseCtx.maxCursorOffset()
                                       : failureMaxCursorOffset;
  result.summary.fullMatch =
      result.summary.entryRuleMatched &&
      result.summary.parsedLength == result.summary.inputSize;
  (void)builder.getRootCstNode();
  result.cst = std::move(cst);
  return result;
}

} // namespace

bool should_fallback_to_parsed_length_snapshot(
    const FailureSnapshot &snapshot, TextOffset parsedLength) noexcept {
  if (parsedLength == 0 ||
      std::ranges::none_of(snapshot.failureLeafHistory,
                           [parsedLength](const FailureLeaf &leaf) {
                             return leaf.endOffset <= parsedLength;
                           })) {
    return false;
  }
  if (!snapshot.hasFailureToken) {
    return false;
  }
  const auto &failureLeaf =
      snapshot.failureLeafHistory[snapshot.failureTokenIndex];
  if (failureLeaf.beginOffset <= parsedLength ||
      snapshot.failureTokenIndex == 0) {
    return false;
  }
  const auto &previousLeaf =
      snapshot.failureLeafHistory[snapshot.failureTokenIndex - 1];
  return previousLeaf.endOffset == failureLeaf.beginOffset &&
         previousLeaf.beginOffset > parsedLength;
}

FailureSnapshot FailureHistoryRecorder::snapshot(TextOffset maxCursorOffset) const {
  FailureSnapshot snapshot{
      .maxCursorOffset = maxCursorOffset,
      .failureLeafHistory =
          std::vector<FailureLeaf>(_visibleLeaves.begin(),
                                   _visibleLeaves.begin() +
                                       static_cast<std::ptrdiff_t>(
                                           _furthestVisibleLeafCount))};
  finalize_failure_token_index(snapshot);
  return snapshot;
}

StrictParseResult run_strict_parse(
    const grammar::ParserRule &entryRule, const Skipper &skipper,
    const text::TextSnapshot &text,
    const utils::CancellationToken &cancelToken,
    FailureHistoryRecorder *failureRecorder) {
  if (failureRecorder == nullptr) {
    return run_strict_parse_with_context(
        entryRule, skipper, text, cancelToken,
        [](CstBuilder &builder, const Skipper &localSkipper,
           const utils::CancellationToken &localCancelToken) {
          return ParseContext{builder, localSkipper, localCancelToken};
        });
  }
  return run_strict_parse_with_context(
      entryRule, skipper, text, cancelToken,
      [failureRecorder](CstBuilder &builder, const Skipper &localSkipper,
                        const utils::CancellationToken &localCancelToken) {
        return TrackedParseContext{builder, localSkipper, *failureRecorder,
                                   localCancelToken};
      });
}

StrictFailureEngineResult
run_strict_parse_with_failure_snapshot(
    const grammar::ParserRule &entryRule, const Skipper &skipper,
    const text::TextSnapshot &text,
    const utils::CancellationToken &cancelToken) {
  StrictFailureEngineResult result;
  FailureHistoryRecorder recorder(text.view().data());
  result.strictResult =
      run_strict_parse(entryRule, skipper, text, cancelToken, &recorder);
  const auto &summary = result.strictResult.summary;
  const auto trackedMaxCursorOffset =
      std::max(summary.maxCursorOffset, recorder.furthestOffset());
  auto snapshot = recorder.snapshot(trackedMaxCursorOffset);
  if (summary.parsedLength < trackedMaxCursorOffset &&
      should_fallback_to_parsed_length_snapshot(snapshot,
                                                summary.parsedLength)) {
    snapshot = recorder.snapshot(summary.parsedLength);
  }
  result.snapshot = std::move(snapshot);
  return result;
}

FailureSnapshot snapshot_from_committed_cst(const RootCstNode &cst,
                                            TextOffset maxCursorOffset) noexcept {
  FailureSnapshot snapshot{.maxCursorOffset = maxCursorOffset,
                           .failureLeafHistory = {}};
  for (NodeId id = 0;; ++id) {
    const auto node = cst.get(id);
    if (!node.valid()) {
      break;
    }
    if (node.isHidden() || !node.isLeaf() || node.getText().empty()) {
      continue;
    }
    snapshot.failureLeafHistory.push_back(
        {.beginOffset = node.getBegin(),
         .endOffset = node.getEnd(),
         .element = node.getGrammarElement()});
  }
  finalize_failure_token_index(snapshot);
  return snapshot;
}

void finalize_failure_token_index(FailureSnapshot &snapshot) noexcept {
  snapshot.failureTokenIndex = 0;
  snapshot.hasFailureToken = false;

  for (std::size_t index = 0; index < snapshot.failureLeafHistory.size(); ++index) {
    const auto &leaf = snapshot.failureLeafHistory[index];
    if (leaf.beginOffset <= snapshot.maxCursorOffset &&
        snapshot.maxCursorOffset < leaf.endOffset) {
      snapshot.failureTokenIndex = index;
      snapshot.hasFailureToken = true;
      return;
    }
  }

  for (std::size_t index = snapshot.failureLeafHistory.size(); index > 0; --index) {
    const auto &leaf = snapshot.failureLeafHistory[index - 1];
    if (leaf.endOffset <= snapshot.maxCursorOffset) {
      snapshot.failureTokenIndex = index - 1;
      snapshot.hasFailureToken = true;
      return;
    }
  }
}

RecoveryWindow compute_recovery_window(const FailureSnapshot &snapshot,
                                       std::uint32_t backwardTokenCount,
                                       std::uint32_t forwardTokenCount,
                                       TextOffset stablePrefixFloorOffset) noexcept {
  const auto effectiveMaxCursorOffset =
      snapshot.failureLeafHistory.empty()
          ? snapshot.maxCursorOffset
          : std::max(snapshot.maxCursorOffset,
                     snapshot.failureLeafHistory.back().endOffset);
  RecoveryWindow window{
      .beginOffset = effectiveMaxCursorOffset,
      .editFloorOffset = effectiveMaxCursorOffset,
      .maxCursorOffset = effectiveMaxCursorOffset,
      .tokenCount = backwardTokenCount,
      .forwardTokenCount = forwardTokenCount,
  };
  if (snapshot.failureLeafHistory.empty() || backwardTokenCount == 0 ||
      !snapshot.hasFailureToken) {
    return window;
  }

  const auto beginIndex = static_cast<std::uint32_t>(
      snapshot.failureTokenIndex > backwardTokenCount
          ? snapshot.failureTokenIndex - backwardTokenCount
          : 0);
  const auto clampedStablePrefixFloorOffset =
      std::min(stablePrefixFloorOffset, effectiveMaxCursorOffset);
  const auto activationBeginOffset =
      snapshot.failureLeafHistory[beginIndex].beginOffset;
  const auto replayBeginOffset =
      clampedStablePrefixFloorOffset == 0
          ? activationBeginOffset
          : std::min(activationBeginOffset, clampedStablePrefixFloorOffset);

  window.visibleLeafBeginIndex = beginIndex;
  if (replayBeginOffset < activationBeginOffset) {
    for (std::uint32_t index = 0; index < beginIndex; ++index) {
      if (snapshot.failureLeafHistory[index].endOffset > replayBeginOffset) {
        window.visibleLeafBeginIndex = index;
        break;
      }
    }
  }
  window.beginOffset = replayBeginOffset;
  window.editFloorOffset =
      std::max(activationBeginOffset, clampedStablePrefixFloorOffset);
  return window;
}

RecoveryWindow compute_recovery_window(const FailureSnapshot &snapshot,
                                       std::uint32_t tokenCount) noexcept {
  return compute_recovery_window(snapshot, tokenCount, tokenCount, 0);
}

namespace {

/// Per-element classification used by
/// `entry_rule_has_unguarded_leading_visible_entry` to walk the entry
/// rule's grammar shape without leaking grammar-recursion details to
/// `RecoverySearch`.
enum class LeadingVisibleEntryKind : std::uint8_t {
  None,
  Unguarded,
  PredicateGuarded,
};

// Upper bound on this static leading-edge walk's recursion depth. A cycle in
// the grammar can only be reached through rule-reference indirection
// (ParserRule/InfixRule/DataTypeRule getElement() pointing back into the
// path, e.g. `a=b; b=a|"z"`), which would otherwise recurse forever and
// overflow the stack on adversarial grammars. This static walk is the only
// recovery grammar traversal without the cycle protection the runtime
// recover() descent has (ActiveRecoveryStack + kMaxRecoveryRuleDepth). The
// cap is generous — far beyond any real grammar's leading-edge nesting, so
// every terminating grammar is classified exactly as before — and on hitting
// it we fall back to the same conservative default the switch already returns
// for leaf/unknown elements.
inline constexpr unsigned kMaxLeadingVisibleEntryDepth = 256u;

[[nodiscard]] LeadingVisibleEntryKind
classify_leading_visible_entry(const grammar::AbstractElement &element,
                               unsigned depth = 0u) noexcept;

// Shared classification for alternation-shaped grammar nodes (`OrderedChoice`
// and `UnorderedGroup`), which expose the same `size()` / `get()` interface
// but share no iterable base: any unguarded alternative makes the whole
// alternation unguarded; otherwise it is `PredicateGuarded` iff some
// alternative is, and `None` when every alternative was `None`.
template <typename Alternation>
[[nodiscard]] LeadingVisibleEntryKind
classify_leading_alternation(const Alternation &alt,
                             unsigned childDepth) noexcept {
  using enum LeadingVisibleEntryKind;
  bool sawGuarded = false;
  for (std::size_t index = 0; index < alt.size(); ++index) {
    const auto *child = alt.get(index);
    if (child == nullptr) {
      continue;
    }
    const auto childKind = classify_leading_visible_entry(*child, childDepth);
    if (childKind == Unguarded) {
      return Unguarded;
    }
    sawGuarded = sawGuarded || childKind == PredicateGuarded;
  }
  return sawGuarded ? PredicateGuarded : None;
}

[[nodiscard]] LeadingVisibleEntryKind
classify_leading_visible_entry(const grammar::AbstractElement &element,
                               unsigned depth) noexcept {
  using enum grammar::ElementKind;
  using enum LeadingVisibleEntryKind;
  if (depth >= kMaxLeadingVisibleEntryDepth) {
    return Unguarded;
  }
  const unsigned childDepth = depth + 1u;
  switch (element.getKind()) {
  case AndPredicate:
  case NotPredicate:
    return PredicateGuarded;
  case Assignment:
    return classify_leading_visible_entry(
        *static_cast<const grammar::Assignment &>(element).getElement(),
        childDepth);
  case ParserRule:
    return classify_leading_visible_entry(
        *static_cast<const grammar::ParserRule &>(element).getElement(),
        childDepth);
  case InfixRule:
    return classify_leading_visible_entry(
        *static_cast<const grammar::InfixRule &>(element).getElement(),
        childDepth);
  case DataTypeRule:
    return classify_leading_visible_entry(
        *static_cast<const grammar::DataTypeRule &>(element).getElement(),
        childDepth);
  case Repetition: {
    const auto &repetition = static_cast<const grammar::Repetition &>(element);
    return repetition.getMin() == 0u
               ? None
               : classify_leading_visible_entry(*repetition.getElement(),
                                                childDepth);
  }
  case Group: {
    const auto &group = static_cast<const grammar::Group &>(element);
    for (std::size_t index = 0; index < group.size(); ++index) {
      const auto *child = group.get(index);
      if (child == nullptr) {
        continue;
      }
      if (const auto childKind = classify_leading_visible_entry(*child, childDepth);
          childKind != None) {
        return childKind;
      }
      if (!child->isNullable()) {
        return None;
      }
    }
    return None;
  }
  case OrderedChoice:
    return classify_leading_alternation(
        static_cast<const grammar::OrderedChoice &>(element), childDepth);
  case UnorderedGroup:
    return classify_leading_alternation(
        static_cast<const grammar::UnorderedGroup &>(element), childDepth);
  case Create:
  case Nest:
    return None;
  case AnyCharacter:
  case CharacterRange:
  case Literal:
  case TerminalRule:
  case InfixOperator:
    return Unguarded;
  }
  return Unguarded;
}

} // namespace

bool entry_rule_has_unguarded_leading_visible_entry(
    const grammar::ParserRule &entryRule) noexcept {
  if (entryRule.getKind() != grammar::ElementKind::ParserRule) {
    return false;
  }
  return classify_leading_visible_entry(*entryRule.getElement()) !=
         LeadingVisibleEntryKind::PredicateGuarded;
}

namespace {

/// Recursion cap of the word-char-mask derivation walk, in the same spirit as
/// `kMaxLeadingVisibleEntryDepth`: rule-reference indirection is cycle-guarded
/// by the visited set, but grammar nesting between rule boundaries is not.
inline constexpr unsigned kMaxWordCharMaskDepth = 256u;

/// DFS state of `derive_word_char_mask`. Rules are visited once (identity on
/// the grammar node's address); the walk carries whether it is inside a
/// terminal rule's body and how many REPEATING `Repetition`s (max > 1 — an
/// option is optionality, not continuation) enclose the current node within
/// the current token. Parser/datatype/infix rule references reset the count
/// (repeating a RULE does not repeat the referenced rule's characters); a
/// terminal referenced from inside another terminal is a lexical fragment of
/// the same token and keeps it.
struct WordCharMaskWalk {
  std::array<bool, 256> accumulated{};
  bool sawContinuationClass = false;
  std::vector<const grammar::AbstractElement *> visitedRules;

  [[nodiscard]] bool markVisited(const grammar::AbstractElement &rule) {
    if (std::ranges::find(visitedRules, &rule) != visitedRules.end()) {
      return false;
    }
    visitedRules.push_back(&rule);
    return true;
  }

  void enterRule(const grammar::AbstractElement &rule,
                 const grammar::AbstractElement *body, bool insideTerminal,
                 unsigned repetitionDepth, unsigned depth) {
    if (body == nullptr || !markVisited(rule)) {
      return;
    }
    visit(*body, insideTerminal, repetitionDepth, depth);
  }

  template <typename Alternation>
  void visitChildren(const Alternation &parent, bool insideTerminal,
                     unsigned repetitionDepth, unsigned childDepth) {
    for (std::size_t index = 0; index < parent.size(); ++index) {
      if (const auto *child = parent.get(index); child != nullptr) {
        visit(*child, insideTerminal, repetitionDepth, childDepth);
      }
    }
  }

  void visit(const grammar::AbstractElement &element, bool insideTerminal,
             unsigned repetitionDepth, unsigned depth) {
    using enum grammar::ElementKind;
    if (depth >= kMaxWordCharMaskDepth) {
      return;
    }
    const unsigned childDepth = depth + 1u;
    switch (element.getKind()) {
    // Predicates are pruned wholesale: a class reachable only through a
    // guard defines a boundary (often a negated set), never the token's own
    // continuation alphabet.
    case AndPredicate:
    case NotPredicate:
      return;
    case CharacterRange:
      if (insideTerminal && repetitionDepth > 0u) {
        const auto &mask =
            static_cast<const grammar::CharacterRange &>(element)
                .characterMask();
        for (std::size_t i = 0; i < mask.size(); ++i) {
          accumulated[i] = accumulated[i] || mask[i];
        }
        sawContinuationClass = true;
      }
      return;
    case Repetition: {
      const auto &repetition = static_cast<const grammar::Repetition &>(element);
      // Only a repetition that can match more than once continues the token;
      // an option (max 1) contributes its classes exactly once, e.g. an
      // optional sign before digits does not make the sign a word character.
      const bool repeats = repetition.getMax() > 1u;
      visit(*repetition.getElement(), insideTerminal,
            repeats ? repetitionDepth + 1u : repetitionDepth, childDepth);
      return;
    }
    case Assignment:
      visit(*static_cast<const grammar::Assignment &>(element).getElement(),
            insideTerminal, repetitionDepth, childDepth);
      return;
    case Group:
      visitChildren(static_cast<const grammar::Group &>(element),
                    insideTerminal, repetitionDepth, childDepth);
      return;
    case OrderedChoice:
      visitChildren(static_cast<const grammar::OrderedChoice &>(element),
                    insideTerminal, repetitionDepth, childDepth);
      return;
    case UnorderedGroup:
      visitChildren(static_cast<const grammar::UnorderedGroup &>(element),
                    insideTerminal, repetitionDepth, childDepth);
      return;
    case TerminalRule:
      // A terminal referenced from inside a terminal is a fragment of the
      // SAME token: its classes repeat whenever the reference site repeats,
      // so the repetition context carries through.
      enterRule(element,
                static_cast<const grammar::TerminalRule &>(element).getElement(),
                /*insideTerminal=*/true,
                insideTerminal ? repetitionDepth : 0u, childDepth);
      return;
    case ParserRule:
      enterRule(element,
                static_cast<const grammar::ParserRule &>(element).getElement(),
                /*insideTerminal=*/false, /*repetitionDepth=*/0u, childDepth);
      return;
    case DataTypeRule:
      enterRule(element,
                static_cast<const grammar::DataTypeRule &>(element).getElement(),
                /*insideTerminal=*/false, /*repetitionDepth=*/0u, childDepth);
      return;
    case InfixRule:
      // Only the primary: operator alphabets are not identifier continuation.
      enterRule(element,
                static_cast<const grammar::InfixRule &>(element).getElement(),
                /*insideTerminal=*/false, /*repetitionDepth=*/0u, childDepth);
      return;
    case AnyCharacter:
    case Literal:
    case InfixOperator:
    case Create:
    case Nest:
      return;
    }
  }
};

} // namespace

WordCharMask derive_word_char_mask(const grammar::ParserRule &entryRule) {
  WordCharMaskWalk walk;
  walk.visitedRules.reserve(32u);
  walk.visit(entryRule, /*insideTerminal=*/false, /*repetitionDepth=*/0u,
             /*depth=*/0u);
  if (!walk.sawContinuationClass) {
    return {};
  }
  WordCharMask mask;
  mask.table = walk.accumulated;
  return mask;
}

} // namespace pegium::parser::detail
