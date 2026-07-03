#pragma once

/// Parser expression representing prioritized alternatives.
///
/// The strict-mode parse path (`parse_strict_impl`, `probe_impl`,
/// `fast_probe_impl`, and the strict overload of `match_choice`) is defined
/// in-class below; it is the hot path exercised by clean input. Recovery-mode
/// members (`parse_recovery_impl`, `make_branch_ops`, `probeRecoverable`,
/// `probeRecoverableAtEntry`, `probeRecoverableAtEntryConsumesVisible`, and
/// the recovery overload of `match_choice`) are declared here and defined
/// out-of-class in OrderedChoiceRecovery.hpp. Expect-mode members
/// (`parse_expect_impl` and its helpers) are declared here and defined
/// out-of-class in OrderedChoiceExpect.hpp. A translation unit that drives an
/// OrderedChoice through recovery or expect mode must include the matching
/// partial header in addition to this one.

#include <array>
#include <concepts>
#include <pegium/core/grammar/OrderedChoice.hpp>
#include <pegium/core/parser/ChoiceAttempt.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ExpectFrontier.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/SkipperBuilder.hpp>
#include <pegium/core/parser/SkipperWrapped.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace pegium::parser {


template <Expression... Elements>
struct OrderedChoice : grammar::OrderedChoice {
  static_assert(sizeof...(Elements) > 1,
                "An OrderedChoice shall contains at least 2 elements.");
  static constexpr bool nullable =
      (... || std::remove_cvref_t<Elements>::nullable);
  static constexpr bool isFailureSafe = true;
  static consteval bool nullable_only_last() {
    constexpr bool flags[] = {std::remove_cvref_t<Elements>::nullable...};
    for (std::size_t i = 0; i + 1 < sizeof...(Elements); ++i)
      if (flags[i])
        return false;
    return true;
  }

  static_assert(nullable_only_last(),
                "OrderedChoice: a nullable alternative must be the last one.");

  constexpr explicit OrderedChoice(std::tuple<Elements...> &&elements)
      : choices{std::move(elements)} {}
  constexpr OrderedChoice(OrderedChoice &&) noexcept = default;
  constexpr OrderedChoice(const OrderedChoice &) = default;
  constexpr OrderedChoice &operator=(OrderedChoice &&) noexcept = default;
  constexpr OrderedChoice &operator=(const OrderedChoice &) = default;

private:
  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;
  friend struct detail::FastProbeAccess;
  friend struct detail::InitAccess;

public:
  /// Recovery-mode; defined out-of-class in OrderedChoiceRecovery.hpp.
  bool probeRecoverable(RecoveryContext &ctx) const;

  template <typename Context> bool probeMatchHere(Context &ctx) const {
    return any_choice(
        [&](const auto &c) { return parser::probe_match_here(c, ctx); });
  }

  /// Recovery-mode; defined out-of-class in OrderedChoiceRecovery.hpp.
  bool probeRecoverableAtEntry(RecoveryContext &ctx) const;

  /// Recovery-mode; defined out-of-class in OrderedChoiceRecovery.hpp.
  bool probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const;

  void init_impl(AstReflectionInitContext &ctx) const {
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (parser::init(std::get<Is>(choices), ctx), ...);
    }(std::make_index_sequence<sizeof...(Elements)>{});
  }

private:

  template <StrictParseModeContext Context>
  bool probe_impl(Context &ctx) const {
    return any_choice([&](const auto &c) { return parser::probe(c, ctx); });
  }

  template <StrictParseModeContext Context>
  bool fast_probe_impl(Context &ctx) const {
    return any_choice(
        [&](const auto &c) { return parser::attempt_fast_probe(ctx, c); });
  }

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
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::ChoiceStrictPasses);
    return match_choice(ctx);
  }

  /// Recovery-mode; defined out-of-class in OrderedChoiceRecovery.hpp.
  bool parse_recovery_impl(RecoveryContext &ctx) const;

  /// Type-erased branch table consumed by `detail::run_choice_recovery`.
  /// The trampolines are static (one set per element type); the element
  /// addresses are per instance, so the table is built on the stack at each
  /// recovery descent. Recovery-mode; defined out-of-class in
  /// OrderedChoiceRecovery.hpp.
  template <std::size_t... Is>
  std::array<detail::ChoiceBranchOps, sizeof...(Elements)>
  make_branch_ops(std::index_sequence<Is...>) const noexcept;

  /// Expect-mode; defined out-of-class in OrderedChoiceExpect.hpp.
  bool parse_expect_impl(ExpectContext &ctx) const;

public:
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
    return SkipperWrapped<OrderedChoice<Elements...>>{
        *this, static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

  template <std::convertible_to<Skipper> LocalSkipper>
  auto with_skipper(LocalSkipper &&localSkipper) && {
    return SkipperWrapped<OrderedChoice<Elements...>>{
        std::move(*this),
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

private:
  using BranchResult = ExpectBranchResult;

public:
  std::tuple<Elements...> choices;

private:
  template <std::size_t... Is>
  const AbstractElement *get_impl(std::size_t elementIndex,
                                  std::index_sequence<Is...>) const noexcept {
    using AccessorFn =
        const AbstractElement *(*)(const OrderedChoice *) noexcept;

    static constexpr std::array<AccessorFn, sizeof...(Elements)> accessors = {
        +[](const OrderedChoice *self) noexcept -> const AbstractElement * {
          return std::addressof(std::get<Is>(self->choices));
        }...};

    return accessors[elementIndex](this);
  }

  /// Folds an OR over every branch with a caller-supplied predicate. The
  /// predicate is invoked with each `choice` element in order; the fold
  /// short-circuits as soon as one branch returns `true`.
  template <typename Predicate, std::size_t... Is>
  [[gnu::always_inline]] bool
  any_choice_impl(Predicate &&pred, std::index_sequence<Is...>) const {
    return (... || pred(std::get<Is>(choices)));
  }

  template <typename Predicate>
  [[gnu::always_inline]] bool any_choice(Predicate &&pred) const {
    return any_choice_impl(std::forward<Predicate>(pred),
                           std::make_index_sequence<sizeof...(Elements)>{});
  }

  template <std::size_t I = 0>
  constexpr const char *terminal_impl(const char *inputBegin) const noexcept
    requires(... && TerminalCapableExpression<Elements>)
  {
    if constexpr (I == sizeof...(Elements)) {
      return nullptr;
    } else {
      const char *matchEnd = std::get<I>(choices).terminal(inputBegin);
      return matchEnd != nullptr ? matchEnd
                                 : terminal_impl<I + 1>(inputBegin);
    }
  }

  template <StrictParseModeContext Context>
  bool match_choice(Context &ctx) const {
    return any_choice(
        [&](const auto &c) { return attempt_parse_strict(ctx, c); });
  }

  /// Recovery-mode; defined out-of-class in OrderedChoiceRecovery.hpp.
  template <RecoveryParseModeContext Context>
  bool match_choice(Context &ctx) const;

  /// Dispatches to the branch at `bestIndex` and invokes `action` on it.
  /// Returns false if `bestIndex` is out of range. The lambda gets the
  /// concrete branch reference, so `std::get<I>` is resolved at compile time
  /// for each unfolded position. Expect-mode; defined out-of-class in
  /// OrderedChoiceExpect.hpp.
  template <typename Action, std::size_t I = 0>
  [[gnu::always_inline]] bool
  dispatch_choice_by_index(std::size_t bestIndex, Action &&action) const;

  /// Expect-mode; defined out-of-class in OrderedChoiceExpect.hpp.
  template <std::size_t... Is>
  void collect_expect_results(ExpectContext &ctx,
                              const ExpectContext::Checkpoint &base,
                              std::array<BranchResult, sizeof...(Elements)> &branches,
                              std::index_sequence<Is...>) const;

  /// Expect-mode; defined out-of-class in OrderedChoiceExpect.hpp.
  template <std::size_t I>
  void collect_expect_result(ExpectContext &ctx,
                             const ExpectContext::Checkpoint &base,
                             BranchResult &result) const;

  /// Expect-mode; defined out-of-class in OrderedChoiceExpect.hpp.
  bool replay_expect_branch_by_index(ExpectContext &ctx,
                                     std::size_t bestIndex) const;

};

template <Expression... Elements>
using OrderedChoiceWithSkipper = SkipperWrapped<OrderedChoice<Elements...>>;

namespace detail {

template <typename T> struct IsOrderedChoiceRaw : std::false_type {};

template <typename... E>
struct IsOrderedChoiceRaw<OrderedChoice<E...>> : std::true_type {};

template <typename... E>
struct IsOrderedChoiceRaw<OrderedChoiceWithSkipper<E...>> : std::true_type {};

template <typename T> struct IsOrderedChoiceFlattenRaw : std::false_type {};

template <typename... E>
struct IsOrderedChoiceFlattenRaw<OrderedChoice<E...>> : std::true_type {};

template <typename C>
  requires IsOrderedChoiceFlattenRaw<std::remove_cvref_t<C>>::value
constexpr decltype(auto) as_choice_tuple(C &&choice) {
  return std::forward<C>(choice).choices;
}

template <Expression Expr>
  requires(!IsOrderedChoiceFlattenRaw<std::remove_cvref_t<Expr>>::value)
constexpr auto as_choice_tuple(Expr &&expr) {
  return std::tuple<ExpressionHolder<Expr>>{std::forward<Expr>(expr)};
}

template <typename... Ts>
constexpr auto make_ordered_choice(std::tuple<Ts...> &&elements) {
  return OrderedChoice<Ts...>{std::move(elements)};
}

} // namespace detail

template <Expression Lhs, Expression Rhs>
constexpr auto operator|(Lhs &&lhs, Rhs &&rhs) {
  return detail::make_ordered_choice(std::tuple_cat(
      detail::as_choice_tuple(std::forward<Lhs>(lhs)),
      detail::as_choice_tuple(std::forward<Rhs>(rhs))));
}

template <typename T>
struct IsOrderedChoice : detail::IsOrderedChoiceRaw<std::remove_cvref_t<T>> {};

} // namespace pegium::parser
