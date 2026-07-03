#pragma once

/// Parser combinator repeating one element `min` to `max` times.
///
/// The strict-mode parse path (`parse_strict_impl`, `probe_impl`,
/// `fast_probe_impl`, the compile-time `terminal()` matcher, and
/// `shapeFacts()`, the eagerly populated grammar-shape-facts cache read by
/// both `init_impl` and recovery) is defined in-class below, together with
/// `IterationElementFollowProbe` and `with_iteration_element_follow`: that
/// follow-probe installation stays templated in-class rather than moving to
/// a partial because the installed probe function identities feed the
/// choice-cache policy fingerprint and must remain per-instantiation
/// symbols. Recovery-mode members (`parse_recovery_impl`, `element_ops`,
/// `probeRecoverable`, `probeRecoverableAtEntry`,
/// `probeRecoverableAtEntryConsumesVisible`) are declared here and defined
/// out-of-class in RepetitionRecovery.hpp. Expect-mode members
/// (`parse_expect_impl`, `try_expect_iteration`) are declared here and
/// defined out-of-class in RepetitionExpect.hpp: unlike Literal/TerminalRule,
/// whose single adapter dispatches recovery and expect internally, Repetition
/// has three distinct top-level dispatch functions (`parse_strict_impl` /
/// `parse_recovery_impl` / `parse_expect_impl`), so recovery and expect split
/// into separate partials the way they do for OrderedChoice. A translation
/// unit that drives a Repetition through recovery or expect mode must
/// include the matching partial header in addition to this one.

#include <concepts>
#include <limits>
#include <memory>
#include <optional>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/DataTypeRule.hpp>
#include <pegium/core/grammar/Group.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/grammar/OrderedChoice.hpp>
#include <pegium/core/grammar/ParserRule.hpp>
#include <pegium/core/grammar/Repetition.hpp>
#include <pegium/core/grammar/TerminalRule.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ExpectFrontier.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/RepetitionRecoveryEngine.hpp>
#include <pegium/core/parser/RepetitionShapeAnalysis.hpp>
#include <pegium/core/parser/SkipperBuilder.hpp>
#include <pegium/core/parser/SkipperWrapped.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <string>

namespace pegium::parser {

template <std::size_t min, std::size_t max, NonNullableExpression Element>
struct Repetition : grammar::Repetition {
  static constexpr bool nullable = min == 0;
  static constexpr bool isFailureSafe =
      min == 0 ||
      (min == 1 &&
       (max != 1 || std::remove_cvref_t<Element>::isFailureSafe));
  static_assert(!(min == 0 && max == 0),
                "A Repetition cannot have both min and max set to 0.");


  explicit constexpr Repetition(Element &&element)
    requires(!std::is_lvalue_reference_v<Element>)
      : _element(std::move(element)) {}

  explicit constexpr Repetition(Element element)
    requires(std::is_lvalue_reference_v<Element>)
      : _element(element) {}

  constexpr Repetition(Repetition &&) noexcept = default;
  constexpr Repetition(const Repetition &) = default;
  constexpr Repetition &operator=(Repetition &&) noexcept = default;
  constexpr Repetition &operator=(const Repetition &) = default;

  const AbstractElement *getElement() const noexcept override {
    return std::addressof(_element);
  }
  std::size_t getMin() const noexcept override { return min; }
  std::size_t getMax() const noexcept override { return max; }

private:
  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;
  friend struct detail::InitAccess;

  static constexpr bool is_optional = (min == 0 && max == 1);
  static constexpr bool is_star =
      (min == 0 && max == std::numeric_limits<std::size_t>::max());
  static constexpr bool is_plus =
      (min == 1 && max == std::numeric_limits<std::size_t>::max());
  static constexpr bool is_fixed = (min == max && min > 0);

  struct ExpectIterationResult {
    bool matched = false;
    bool blocked = false;
    bool progressed = false;
  };

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      return parse_strict_impl(ctx);
    } else if constexpr (RecoveryParseModeContext<Context>) {
      return parse_recovery_impl(ctx);
    } else {
      return parse_expect_impl(ctx);
    }
  }

  template <StrictParseModeContext Context>
  bool parse_strict_impl(Context &ctx) const {
    if constexpr (is_optional) {
      (void)attempt_parse_strict(ctx, _element);
      return true;
    } else if constexpr (is_star) {
      if (!attempt_parse_strict(ctx, _element)) {
        return true;
      }
      auto checkpointAfterItem = ctx.mark();
      while (true) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
    } else if constexpr (is_plus) {
      if (!attempt_parse_strict(ctx, _element)) {
        return false;
      }
      auto checkpointAfterItem = ctx.mark();
      while (true) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
    } else if constexpr (is_fixed) {
      if (!parse(_element, ctx)) {
        return false;
      }
      for (std::size_t repetitionIndex = 1; repetitionIndex < min;
           ++repetitionIndex) {
        ctx.skip();
        if (!parse(_element, ctx)) {
          return false;
        }
      }
      return true;
    } else if constexpr (min == 0) {
      if (!attempt_parse_strict(ctx, _element)) {
        return true;
      }
      auto checkpointAfterItem = ctx.mark();
      std::size_t repetitionCount = 1;
      for (; repetitionCount < max; ++repetitionCount) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
      return true;
    } else {
      if (!attempt_parse_strict(ctx, _element)) {
        return false;
      }
      auto checkpointAfterItem = ctx.mark();
      std::size_t repetitionCount = 1;
      for (; repetitionCount < min; ++repetitionCount) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return false;
        }
        checkpointAfterItem = ctx.mark();
      }

      for (; repetitionCount < max; ++repetitionCount) {
        ctx.skip();
        if (!attempt_parse_strict(ctx, _element)) {
          ctx.rewind(checkpointAfterItem);
          return true;
        }
        checkpointAfterItem = ctx.mark();
      }
      return true;
    }
  }

  /// Recovery-mode entry point, dispatched at compile time on the repetition
  /// shape (after the descent-inactive guard falls back to the strict path):
  ///   - is_optional (`Repetition<0,1>`): strict no-edit attempt first; on
  ///     failure rewind, then a fast attempt gated by `has_entry_recovery_signal`
  ///     (no signal and not started => skip the option), then one recovery
  ///     iteration, then skip-if the suffix stays recoverable without it.
  ///   - is_star (`Repetition<0,N>`): `parse_zero_min_repetition_recovery`.
  ///   - is_plus (`Repetition<1,N>`): one mandatory recovery iteration, then a
  ///     skip-between loop until an iteration fails (a blocked frontier either
  ///     stops cleanly after window progress or fails the rule).
  ///   - is_fixed (`min == max`): exactly `min` strict parses, no recovery.
  ///   - general (`min..max`): `min` mandatory strict parses, then the zero-min
  ///     recovery (if `min == 0`) or the plus-style skip-between loop up to max.
  /// The per-iteration machinery is type-erased into the shared engine
  /// (RepetitionRecoveryEngine.cpp), reached through `element_ops()`; the
  /// per-iteration plan choice lives in the engine's
  /// `repetition_try_recovery_iteration` -> `enumerate_iteration_plans`.
  /// Recovery-mode; defined out-of-class in RepetitionRecovery.hpp.
  bool parse_recovery_impl(RecoveryContext &ctx) const;

  /// Expect-mode; defined out-of-class in RepetitionExpect.hpp.
  bool parse_expect_impl(ExpectContext &ctx) const;

public:
  /// Recovery-mode; defined out-of-class in RepetitionRecovery.hpp.
  bool probeRecoverable(RecoveryContext &ctx) const;

  /// Recovery-mode; defined out-of-class in RepetitionRecovery.hpp.
  bool probeRecoverableAtEntry(RecoveryContext &ctx) const;

  /// Recovery-mode; defined out-of-class in RepetitionRecovery.hpp.
  bool probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const;

  void init_impl(AstReflectionInitContext &ctx) const {
    parser::init(_element, ctx);
    // Populate the shape-facts cache during the sequential init pass so it is
    // never written lazily during parsing: one parser instance is shared across
    // parallel document builds, and a first-touch write in shapeFacts() on this
    // mutable cache would be a data race. The facts are purely structural, so
    // computing them here yields the same result as the former lazy path.
    (void)shapeFacts();
  }

private:
  template <StrictParseModeContext Context>
  bool probe_impl(Context &ctx) const {
    return attempt_fast_probe(ctx, _element);
  }

  template <StrictParseModeContext Context>
  bool fast_probe_impl(Context &ctx) const {
    return attempt_fast_probe(ctx, _element);
  }

  struct IterationElementFollowProbe {
    const Repetition *self = nullptr;
    RecoveryContext::FollowProbeFn outerStrict = nullptr;
    const void *outerStrictData = nullptr;
    RecoveryContext::FollowProbeFn outerRecoverable = nullptr;
    const void *outerRecoverableData = nullptr;
    RecoveryContext::FollowProbeFn outerRecoverableConsumesVisible = nullptr;
    const void *outerRecoverableConsumesVisibleData = nullptr;

    template <auto FnPtr, auto DataPtr>
    static bool dispatch_outer(RecoveryContext &ctx, const void *data) {
      const auto &probe =
          *static_cast<const IterationElementFollowProbe *>(data);
      if (attempt_fast_probe(ctx, probe.self->_element)) {
        return true;
      }
      auto fn = probe.*FnPtr;
      return fn != nullptr && fn(ctx, probe.*DataPtr);
    }

    static bool strict(RecoveryContext &ctx, const void *data) {
      return dispatch_outer<&IterationElementFollowProbe::outerStrict,
                            &IterationElementFollowProbe::outerStrictData>(ctx,
                                                                           data);
    }
    static bool recoverable(RecoveryContext &ctx, const void *data) {
      return dispatch_outer<&IterationElementFollowProbe::outerRecoverable,
                            &IterationElementFollowProbe::outerRecoverableData>(
          ctx, data);
    }
    static bool recoverableConsumesVisible(RecoveryContext &ctx,
                                           const void *data) {
      return dispatch_outer<
          &IterationElementFollowProbe::outerRecoverableConsumesVisible,
          &IterationElementFollowProbe::outerRecoverableConsumesVisibleData>(
          ctx, data);
    }
  };

  template <typename Fn>
  bool with_iteration_element_follow(RecoveryContext &ctx, Fn &&fn) const {
    IterationElementFollowProbe probe{
        .self = this,
        .outerStrict = ctx._followProbes.strict.fn,
        .outerStrictData = ctx._followProbes.strict.data,
        .outerRecoverable = ctx._followProbes.recoverable.fn,
        .outerRecoverableData = ctx._followProbes.recoverable.data,
        .outerRecoverableConsumesVisible =
            ctx._followProbes.recoverableConsumesVisible.fn,
        .outerRecoverableConsumesVisibleData =
            ctx._followProbes.recoverableConsumesVisible.data,
    };
    auto followGuard = ctx.withFollowProbe(
        &IterationElementFollowProbe::strict, &probe,
        &IterationElementFollowProbe::recoverable, &probe,
        &IterationElementFollowProbe::recoverableConsumesVisible, &probe,
        /*stableOwner=*/this);
    (void)followGuard;
    return std::forward<Fn>(fn)();
  }

  /// Type-erased element view consumed by the shared recovery engine
  /// (RepetitionRecoveryEngine.cpp). The trampolines are static (one set per
  /// element type); `self` and the shape-facts pointer are per instance, so
  /// the table is built on the stack at each recovery descent. The follow
  /// installation routes back through `with_iteration_element_follow`, so the
  /// installed probe identities stay per-instantiation (they feed the
  /// choice-cache policy fingerprint). Recovery-mode; defined out-of-class in
  /// RepetitionRecovery.hpp.
  [[nodiscard]] detail::RepetitionElementOps element_ops() const noexcept;

public:

  constexpr const char *terminal(const char *begin) const noexcept
    requires TerminalCapableExpression<Element>
  {
    // optional
    if constexpr (is_optional) {
      const char *matchEnd = _element.terminal(begin);
      return matchEnd != nullptr ? matchEnd : begin;
    }
    // zero or more
    else if constexpr (is_star) {
      const char *cursor = begin;
      while (true) {
        const char *matchEnd = _element.terminal(cursor);
        if (matchEnd == nullptr) {
          break;
        }
        cursor = matchEnd;
      }
      return cursor;
    }
    // one or more
    else if constexpr (is_plus) {

      const char *cursor = _element.terminal(begin);
      if (cursor == nullptr)
        return cursor;

      while (true) {
        const char *matchEnd = _element.terminal(cursor);
        if (matchEnd == nullptr) {
          break;
        }
        cursor = matchEnd;
      }
      return cursor;
    }
    // only min/max times
    else if constexpr (is_fixed) {
      const char *cursor = begin;
      for (std::size_t repetitionIndex = 0; repetitionIndex < min;
           ++repetitionIndex) {
        cursor = _element.terminal(cursor);
        if (cursor == nullptr) {
          return cursor;
        }
      }
      return cursor;
    }
    // other cases
    else {

      const char *cursor = begin;
      std::size_t repetitionCount = 0;
      for (; repetitionCount < min; ++repetitionCount) {
        cursor = _element.terminal(cursor);
        if (cursor == nullptr) {
          return cursor;
        }
      }

      for (; repetitionCount < max; ++repetitionCount) {
        const char *matchEnd = _element.terminal(cursor);
        if (matchEnd == nullptr) {
          break;
        }
        cursor = matchEnd;
      }

      return cursor;
    }
  }
  constexpr const char *terminal(const std::string &text) const noexcept
    requires TerminalCapableExpression<Element>
  {
    return terminal(text.c_str());
  }

  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

  template <std::convertible_to<Skipper> LocalSkipper>
    requires std::copy_constructible<ExpressionHolder<Element>>
  auto skip(LocalSkipper &&localSkipper) const & {
    return with_skipper(std::forward<LocalSkipper>(localSkipper));
  }

  template <std::convertible_to<Skipper> LocalSkipper>
  auto skip(LocalSkipper &&localSkipper) && {
    return std::move(*this).with_skipper(std::forward<LocalSkipper>(localSkipper));
  }

  template <typename... SkipperParts>
    requires((... && (detail::IsHiddenRules_v<SkipperParts> ||
                     detail::IsIgnoredRules_v<SkipperParts>))) &&
            std::copy_constructible<ExpressionHolder<Element>>
  auto skip(SkipperParts &&...parts) const & {
    return with_skipper(parser::skip(std::forward<SkipperParts>(parts)...));
  }

  template <typename... SkipperParts>
    requires((... && (detail::IsHiddenRules_v<SkipperParts> ||
                     detail::IsIgnoredRules_v<SkipperParts>)))
  auto skip(SkipperParts &&...parts) && {
    return std::move(*this).with_skipper(
        parser::skip(std::forward<SkipperParts>(parts)...));
  }

  template <std::convertible_to<Skipper> LocalSkipper>
    requires std::copy_constructible<ExpressionHolder<Element>>
  auto with_skipper(LocalSkipper &&localSkipper) const & {
    return SkipperWrapped<Repetition<min, max, Element>>{
        *this, static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

  template <std::convertible_to<Skipper> LocalSkipper>
  auto with_skipper(LocalSkipper &&localSkipper) && {
    return SkipperWrapped<Repetition<min, max, Element>>{
        std::move(*this),
        static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

private:
  ExpressionHolder<Element> _element;

  /// Pure-grammar facts derived from `_element` once per Repetition instance
  /// (see RepetitionShapeAnalysis.hpp), cached so each iteration boundary reads
  /// a single field.
  mutable std::optional<detail::ShapeFacts> _shapeFacts;

  [[nodiscard]] const detail::ShapeFacts &shapeFacts() const noexcept {
    if (!_shapeFacts.has_value()) {
      detail::ShapeFacts f;
      f.hasInsertableTerminal =
          detail::starts_with_insertable_terminal(_element);
      f.hasRequiredLiteralAnchor =
          detail::starts_with_required_literal_anchor(_element);
      f.hasSyntheticTerminalThenLexicalTail =
          detail::starts_with_synthetic_terminal_then_unassigned_lexical_tail(
              _element);
      _shapeFacts = f;
    }
    return *_shapeFacts;
  }

  /// Expect-mode; defined out-of-class in RepetitionExpect.hpp.
  ExpectIterationResult try_expect_iteration(ExpectContext &ctx,
                                             bool skipBefore) const;

};

template <std::size_t min, std::size_t max, NonNullableExpression Element>
using RepetitionWithSkipper =
    SkipperWrapped<Repetition<min, max, Element>>;

/// Make the `element` optional (repeated zero or one)
/// @tparam Element the expression to repeat
/// @param element the element to be optional
/// @return a repetition of zero or one `element`.
template <NonNullableExpression Element>
constexpr auto option(Element &&element) {
  return Repetition<0, 1, Element>{std::forward<Element>(element)};
}

/// Repeat the `element` zero or more
/// @tparam Element the expression to repeat
/// @param element the element to be repeated
/// @return a repetition of zero or more `element`.
template <NonNullableExpression Element>
constexpr auto many(Element &&element) {
  return Repetition<0, std::numeric_limits<std::size_t>::max(), Element>{
      std::forward<Element>(element)};
}

/// Repeat the `element` one or more
/// @tparam Element the expression to repeat
/// @param element the element to be repeated
/// @return a repetition of one or more `element`.
template <NonNullableExpression Element>
constexpr auto some(Element &&element) {
  return Repetition<1, std::numeric_limits<std::size_t>::max(), Element>{
      std::forward<Element>(element)};
}

/// Repeat the `element` one or more using a `separator`:
/// `element (separator element)*`
/// @tparam Element the expression to repeat
/// @tparam Sep the expression to use as separator
/// @param element the element to be repeated
/// @param separator the separator to be used between elements
/// @return a repetition of one or more `element` with a `separator`.
template <NonNullableExpression Element, Expression Sep>
constexpr auto some(Element &&element, Sep &&separator) {
  return std::forward<Element>(element) +
         many(std::forward<Sep>(separator) + std::forward<Element>(element));
}

/// Repeat the `element` zero or more using a `separator`
/// `(element (separator element)*)?`
/// @tparam Element the expression to repeat
/// @tparam Sep the expression to use as separator
/// @param element the element to be repeated
/// @param separator the separator to be used between elements
/// @return a repetition of zero or more `element` with a `separator`.
template <NonNullableExpression Element, Expression Sep>
constexpr auto many(Element &&element, Sep &&separator) {
  return option(
      some(std::forward<Element>(element), std::forward<Sep>(separator)));
}

/// Repeat the `element` `count` times.
/// @tparam count the count of repetitions
/// @tparam Element the expression to repeat
/// @param element the elements to be repeated
/// @return a repetition of `count` `element`.
template <std::size_t count, NonNullableExpression Element>
constexpr auto repeat(Element &&element) {
  return Repetition<count, count, Element>{std::forward<Element>(element)};
}

/// Repeat the `element` between `min` and `max` times.
/// @tparam min the min number of occurence (inclusive)
/// @tparam max the max number of occurence (inclusive)
/// @tparam Element the expression to repeat
/// @param element the elements to be repeated
/// @return a repetition of `min` to `max` `element`.
template <std::size_t min, std::size_t max, NonNullableExpression Element>
constexpr auto repeat(Element &&element) {
  return Repetition<min, max, Element>{std::forward<Element>(element)};
}

} // namespace pegium::parser
