#pragma once

/// Parser expression representing an ordered sequence of child expressions.
///
/// The strict-mode parse path (`parse_elements_strict`, `fast_probe_impl`,
/// `probe_impl`, `fast_probe_elements`) is defined in-class below, together
/// with all mode-agnostic plumbing (`terminal`, `skip`/`with_skipper`,
/// `get`/`size`/`isNullable`, `init_impl`/`init_elements`) and
/// `parse_elements`, the per-element dispatcher shared by every mode: it is
/// called by `parse_impl` and, for its recursive tail continuation, by each
/// mode-specific handler (`parse_elements_strict`/`parse_elements_recovery`/
/// `parse_elements_expect`), so it must stay reachable from all three and
/// cannot be classified as belonging to a single mode.
///
/// Recovery-mode members are declared here and defined out-of-class in
/// GroupRecovery.hpp: the public `probeRecoverable`/
/// `probeRecoverableAtEntry`/`probeRecoverableAtEntryConsumesVisible`
/// accessors, `element_is_terminal_like_for_missing_insert`,
/// `note_sequence_transition`, the 4-transition `parse_elements_recovery`
/// driver, `sequence_ops` (the per-visit type-erased ops-table builder
/// consumed by SequenceRecoveryEngine.cpp), `with_current_follow_probe`, and
/// the index-recursive probe walkers that back the driver
/// (`nullable_element_allows_recovery_skip`, the `walk_suffix_probes`
/// skeleton with its `detail::Suffix*Policy` table,
/// `probe_recoverable_entry_consumes_visible_elements`,
/// `recoverable_follow_consumes_visible_probe`, `analyze_suffix_entry_facts`,
/// `probe_nullable_current_looks_started`, `previous_element_is_terminalish`,
/// `previous_nullable_sibling_owns_cursor`,
/// `nullable_current_consumed_visible_since`,
/// `allows_synthetic_terminal_insert`, `suffix_after_is_fully_nullable`,
/// `first_required_tail_element`, `tail_supports_single_terminal_recovery`,
/// `parse_clean_tail_without_edits`). Every one of these is reachable only
/// from the recovery driver or from each other, never from the strict or
/// expect paths, despite several sitting textually next to strict/mode-
/// agnostic members in earlier revisions of this file. `SequenceElementTransition`
/// and `RecoverableFollowConsumesVisibleProbe` are small recovery-only types
/// rather than logic, so they stay defined in-class alongside the strict
/// members instead of being forward-declared and completed out-of-class.
///
/// Expect-mode (`parse_elements_expect`) is declared here and defined
/// out-of-class in GroupExpect.hpp. A translation unit that drives a Group
/// through recovery or expect mode must include the matching partial header
/// in addition to this one.

#include <array>
#include <concepts>
#include <tuple>
#include <utility>

#include <pegium/core/grammar/Group.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/EditableRecoverySupport.hpp>
#include <pegium/core/parser/ExpectFrontier.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/SequenceRecoveryEngine.hpp>
#include <pegium/core/parser/SkipperBuilder.hpp>
#include <pegium/core/parser/SkipperWrapped.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <string>
#include <string_view>

namespace pegium::parser {

template <std::size_t min, std::size_t max, NonNullableExpression Element>
struct Repetition;

namespace detail {

template <typename Element>
struct NullableSiblingOwnershipPolicy {
  static constexpr bool allowRecoverableContinuation = true;
};

template <std::size_t min, std::size_t max, NonNullableExpression Element>
struct NullableSiblingOwnershipPolicy<Repetition<min, max, Element>> {
  static constexpr bool allowRecoverableContinuation = max != 1u;
};

template <std::size_t min, std::size_t max, NonNullableExpression Element>
struct NullableSiblingOwnershipPolicy<
    SkipperWrapped<Repetition<min, max, Element>>> {
  static constexpr bool allowRecoverableContinuation = max != 1u;
};

template <typename Element>
inline constexpr bool nullable_sibling_allows_recoverable_continuation_v =
    NullableSiblingOwnershipPolicy<std::remove_cvref_t<Element>>::
        allowRecoverableContinuation;

} // namespace detail

template <Expression... Elements> struct Group : grammar::Group {
  static constexpr bool nullable =
      (... && std::remove_cvref_t<Elements>::nullable);
  static constexpr bool isFailureSafe = false;
  static_assert(sizeof...(Elements) > 1,
                "A Group shall contains at least 2 elements.");

  constexpr explicit Group(std::tuple<Elements...> &&tupleElements)
      : elements{std::move(tupleElements)} {}

protected:
  constexpr explicit Group(const std::tuple<Elements...> &tupleElements)
      : elements{tupleElements} {}

public:
  constexpr Group(Group &&) noexcept = default;
  constexpr Group(const Group &) = default;
  constexpr Group &operator=(Group &&) noexcept = default;
  constexpr Group &operator=(const Group &) = default;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  bool probeRecoverable(RecoveryContext &ctx) const;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  bool probeRecoverableAtEntry(RecoveryContext &ctx) const;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  bool probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const;

  constexpr const char *terminal(const char *begin) const noexcept
    requires(... && TerminalCapableExpression<Elements>)
  {
    return terminal_impl(begin);
  }
  constexpr const char *terminal(const std::string &text) const noexcept
    requires(... && TerminalCapableExpression<Elements>)
  {
    return terminal(text.c_str());
  }

  template <std::convertible_to<Skipper> LocalSkipper>
    requires std::copy_constructible<std::tuple<Elements...>>
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
            std::copy_constructible<std::tuple<Elements...>>
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
    requires std::copy_constructible<std::tuple<Elements...>>
  auto with_skipper(LocalSkipper &&localSkipper) const & {
    return SkipperWrapped<Group<Elements...>>{
        Group<Elements...>{elements},
        static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

  template <std::convertible_to<Skipper> LocalSkipper>
  auto with_skipper(LocalSkipper &&localSkipper) && {
    return SkipperWrapped<Group<Elements...>>{
        Group<Elements...>{std::move(elements)},
        static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

  const AbstractElement *get(std::size_t elementIndex) const noexcept override {
    if (elementIndex >= sizeof...(Elements))
      return nullptr;
    return get_impl(elementIndex,
                    std::make_index_sequence<sizeof...(Elements)>());
  }

  std::size_t size() const noexcept override { return sizeof...(Elements); }
  constexpr bool isNullable() const noexcept override {
    return nullable;
  }
  std::tuple<Elements...> elements;

  /// Recovery-mode (called only from recovery-only probes below); defined
  /// out-of-class in GroupRecovery.hpp.
  [[nodiscard]] static constexpr bool
  element_is_terminal_like_for_missing_insert(
      const grammar::AbstractElement &element) noexcept;

  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;
  friend struct detail::FastProbeAccess;
  friend struct detail::InitAccess;

  template <StrictParseModeContext Context>
  bool fast_probe_impl(Context &ctx) const {
    return fast_probe_elements<0>(ctx);
  }

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    return parse_elements<Context, 0>(ctx);
  }

  void init_impl(AstReflectionInitContext &ctx) const {
    init_elements<0>(ctx);
  }

  /// Sequential dispatcher per-mode. Each mode's logic lives in a
  /// dedicated helper (`parse_elements_strict`, `parse_elements_recovery`,
  /// `parse_elements_expect`) so the strict-only path is physically
  /// separated from the recovery branch. Keeping the strict helper free of
  /// recovery symbols is a maintenance invariant, not currently enforced by
  /// an automated check.
  template <ParseModeContext Context, std::size_t I>
  bool parse_elements(
      Context &ctx,
      bool previousNullableSiblingConsumedVisible = false) const {
    if constexpr (I == sizeof...(Elements)) {
      return true;
    } else {
      const char *cursorBeforeSkip = nullptr;
      if constexpr (I > 0) {
        cursorBeforeSkip = ctx.cursor();
        ctx.skip();
      }
      if constexpr (StrictParseModeContext<Context>) {
        return parse_elements_strict<Context, I>(ctx);
      } else if constexpr (RecoveryParseModeContext<Context>) {
        return parse_elements_recovery<Context, I>(
            ctx, cursorBeforeSkip, previousNullableSiblingConsumedVisible);
      } else {
        return parse_elements_expect<Context, I>(ctx);
      }
    }
  }

private:
  template <StrictParseModeContext Context, std::size_t I>
  bool parse_elements_strict(Context &ctx) const {
    if (!parse(std::get<I>(elements), ctx)) {
      return false;
    }
    return parse_elements<Context, I + 1>(ctx);
  }

  /// The four labeled transitions the recovery state machine in
  /// `parse_elements_recovery` can take at a sequence-element boundary.
  /// Purely observational — `note_sequence_transition` maps each to a
  /// step-trace counter (a release no-op unless `PEGIUM_ENABLE_STEP_TRACE`)
  /// so a differential fuzzer can report *which* transition fired, by name,
  /// on a given input. `RepairCurrent` exists only in prose (the strict
  /// attempt's on-failure recovery), not as a distinct branch, and so is
  /// deliberately absent here.
  enum class SequenceElementTransition {
    InsertMissingCurrent,
    SkipNullable,
    KeepCurrent,
    RepairTail,
  };

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  static void
  note_sequence_transition(SequenceElementTransition transition) noexcept;

  /// The 4-transition recovery driver; recovery-mode, defined out-of-class
  /// in GroupRecovery.hpp.
  template <RecoveryParseModeContext Context, std::size_t I>
  bool parse_elements_recovery(Context &ctx,
                                const char *cursorBeforeSkip,
                                bool previousNullableSiblingConsumedVisible) const;

  /// Expect-mode; defined out-of-class in GroupExpect.hpp.
  template <ParseModeContext Context, std::size_t I>
  bool parse_elements_expect(Context &ctx) const;

  struct RecoverableFollowConsumesVisibleProbe {
    const Group *self = nullptr;
    RecoveryContext::FollowProbeFn outerRecoverableConsumesVisible = nullptr;
    const void *outerRecoverableConsumesVisibleData = nullptr;
  };

  template <StrictParseModeContext Context>
  bool probe_impl(Context &ctx) const {
    return fast_probe_elements<0>(ctx);
  }

  template <std::size_t I, StrictParseModeContext Context>
  bool fast_probe_elements(Context &ctx) const {
    if constexpr (I == sizeof...(Elements)) {
      return false;
    } else {
      const auto &current = std::get<I>(elements);
      if (attempt_fast_probe(ctx, current)) {
        return true;
      }
      if constexpr (std::remove_cvref_t<decltype(current)>::nullable) {
        return fast_probe_elements<I + 1>(ctx);
      } else {
        return false;
      }
    }
  }

  /// Recovery-mode (used only by `sequence_ops`); defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] constexpr bool
  nullable_element_allows_recovery_skip() const noexcept;

  /// Shared recursion skeleton of the boolean suffix probe walkers: visit
  /// elements from `I`, stop with `true` on the first element whose
  /// `Policy::probe` hits, honour the policy's AndPredicate opacity, fall
  /// through nullable elements, and at the first required element either
  /// stop or continue under `Policy::RequiredTail`. Policies live in
  /// `detail` (GroupRecovery.hpp) next to the definition. Recovery-mode;
  /// defined out-of-class in GroupRecovery.hpp.
  template <typename Policy, std::size_t I>
  bool walk_suffix_probes(RecoveryContext &ctx) const;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  template <std::size_t I>
  bool probe_recoverable_entry_consumes_visible_elements(
      RecoveryContext &ctx,
      bool allowSyntheticLeadingTerminalContinuation = false,
      RecoveryContext::FollowProbeFn outerRecoverableConsumesVisible = nullptr,
      const void *outerRecoverableConsumesVisibleData = nullptr) const;

  /// Recovery-mode (installed as the recoverable-consumes-visible follow
  /// probe by `with_current_follow_probe`); defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  static bool recoverable_follow_consumes_visible_probe(RecoveryContext &ctx,
                                                        const void *data);

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] detail::SequenceSuffixEntryFacts
  analyze_suffix_entry_facts(RecoveryContext &ctx) const;

  /// Probe whether the nullable current element appears to have
  /// started accepting at the cursor. For non-nullable elements,
  /// structurally false. For predicate elements (`AndPredicate`,
  /// `NotPredicate`), false because predicates do not consume input.
  /// Probe is non-mutating: cursor and furthest explored cursor
  /// are restored. Recovery-mode; defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] bool
  probe_nullable_current_looks_started(RecoveryContext &ctx) const;

  /// Recovery-mode (used only by `sequence_ops`); defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] constexpr bool previous_element_is_terminalish() const noexcept;

  /// Type-erased view of the element-I boundary consumed by the shared
  /// sequence recovery engine (SequenceRecoveryEngine.cpp). The trampolines
  /// are static (one set per (Group, I)); `self` and the element pointer are
  /// per instance, so the table is built on the stack at each recovery
  /// descent. The follow installation routes back through
  /// `with_current_follow_probe<I>`, so the installed probe identities stay
  /// per-instantiation (they feed the choice-cache policy fingerprint).
  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] detail::SequenceElementOps sequence_ops() const noexcept;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  template <std::size_t I, typename Fn>
  bool with_current_follow_probe(RecoveryContext &ctx, Fn &&fn) const;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] bool
  previous_nullable_sibling_owns_cursor(
      RecoveryContext &ctx,
      bool previousNullableSiblingConsumedVisible) const;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] bool nullable_current_consumed_visible_since(
      const RecoveryContext &ctx,
      const RecoveryContext::Checkpoint &checkpoint) const noexcept;

  /// Recovery-mode; defined out-of-class in GroupRecovery.hpp.
  template <typename Current>
  [[nodiscard]] static constexpr bool
  allows_synthetic_terminal_insert(
      RecoveryContext &ctx, const Current &current,
      bool allowSequenceLiteralInsert = false) noexcept;

  /// Recovery-mode (used only by `sequence_ops`,
  /// `previous_nullable_sibling_owns_cursor`, and
  /// `tail_supports_single_terminal_recovery`); defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  static constexpr bool suffix_after_is_fully_nullable() noexcept;

  /// Recovery-mode (used only by `sequence_ops`); defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] constexpr const AbstractElement *
  first_required_tail_element() const noexcept;

  /// Recovery-mode (used only by `sequence_ops`); defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  [[nodiscard]] constexpr bool
  tail_supports_single_terminal_recovery() const noexcept;

  /// Recovery-mode (used only by `sequence_ops`); defined out-of-class in
  /// GroupRecovery.hpp.
  template <std::size_t I>
  bool parse_clean_tail_without_edits(RecoveryContext &ctx) const;

  template <std::size_t I>
  void init_elements(AstReflectionInitContext &ctx) const {
    if constexpr (I < sizeof...(Elements)) {
      parser::init(std::get<I>(elements), ctx);
      init_elements<I + 1>(ctx);
    }
  }

  template <std::size_t... Is>
  const AbstractElement *get_impl(std::size_t elementIndex,
                                  std::index_sequence<Is...>) const noexcept {
    using AccessorFn = const AbstractElement *(*)(const Group *) noexcept;

    static constexpr std::array<AccessorFn, sizeof...(Elements)> accessors = {
        +[](const Group *self) noexcept -> const AbstractElement * {
          return std::addressof(std::get<Is>(self->elements));
        }...};

    return accessors[elementIndex](this);
  }

  template <std::size_t I = 0>
  constexpr const char *terminal_impl(const char *cursor) const noexcept
    requires(... && TerminalCapableExpression<Elements>)
  {
    if constexpr (I == sizeof...(Elements)) {
      return cursor;
    } else {
      const char *matchEnd = std::get<I>(elements).terminal(cursor);
      return matchEnd != nullptr ? terminal_impl<I + 1>(matchEnd)
                                 : nullptr;
    }
  }
};

template <Expression... Elements>
using GroupWithSkipper = SkipperWrapped<Group<Elements...>>;

namespace detail {

template <typename T> struct IsGroupRaw : std::false_type {};
template <Expression... E> struct IsGroupRaw<Group<E...>> : std::true_type {};

template <typename G>
  requires IsGroupRaw<std::remove_cvref_t<G>>::value
constexpr decltype(auto) as_group_tuple(G &&group) {
  return std::forward<G>(group).elements;
}

template <Expression Expr>
  requires(!IsGroupRaw<std::remove_cvref_t<Expr>>::value)
constexpr auto as_group_tuple(Expr &&expr) {
  return std::tuple<ExpressionHolder<Expr>>{std::forward<Expr>(expr)};
}

template <typename... Ts>
constexpr auto make_group(std::tuple<Ts...> &&elements) {
  return Group<Ts...>{std::move(elements)};
}

} // namespace detail

template <Expression Lhs, Expression Rhs>
constexpr auto operator+(Lhs &&lhs, Rhs &&rhs) {
  return detail::make_group(std::tuple_cat(
      detail::as_group_tuple(std::forward<Lhs>(lhs)),
      detail::as_group_tuple(std::forward<Rhs>(rhs))));
}
} // namespace pegium::parser
