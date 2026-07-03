#pragma once
/// Parser expression matching all child expressions in any order.
#include <algorithm>
#include <array>
#include <concepts>
#include <pegium/core/grammar/UnorderedGroup.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ExpectFrontier.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/SkipperBuilder.hpp>
#include <pegium/core/parser/SkipperWrapped.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <string>
#include <tuple>

namespace pegium::parser {


template <Expression... Elements>
struct UnorderedGroup : grammar::UnorderedGroup {
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = true;
  static_assert(sizeof...(Elements) > 1,
                "An UnorderedGroup shall contains at least 2 elements.");
  static_assert(
      (!std::remove_cvref_t<Elements>::nullable && ...),
      "An UnorderedGroup cannot be initialized with nullable elements.");

  constexpr explicit UnorderedGroup(std::tuple<Elements...> &&tupleElements)
      : elements{std::move(tupleElements)} {}

  constexpr UnorderedGroup(UnorderedGroup &&) noexcept = default;
  constexpr UnorderedGroup(const UnorderedGroup &) = default;
  constexpr UnorderedGroup &operator=(UnorderedGroup &&) noexcept = default;
  constexpr UnorderedGroup &operator=(const UnorderedGroup &) = default;

private:
  friend struct detail::ParseAccess;
  friend struct detail::InitAccess;

public:
  void init_impl(AstReflectionInitContext &ctx) const { init_elements<0>(ctx); }

private:

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    using enum detail::StepCounter;
    if constexpr (StrictParseModeContext<Context>) {
      PEGIUM_STEP_TRACE_INC(UnorderedStrictPasses);
      return parse_group(ctx);
    } else if constexpr (RecoveryParseModeContext<Context>) {
      PEGIUM_STEP_TRACE_INC(UnorderedRecoverCalls);

      const auto entryCheckpoint = ctx.mark();

      if (!ctx.isInRecoveryPhase()) {
        const bool strictMatched =
            parse_group(static_cast<TrackedParseContext &>(ctx));
        PEGIUM_STEP_TRACE_INC(UnorderedStrictPasses);
        if (strictMatched) {
          return true;
        }
        ctx.rewind(entryCheckpoint);
      }

      PEGIUM_STEP_TRACE_INC(UnorderedEditablePasses);
      if (parse_group(ctx)) {
        return true;
      }
      ctx.rewind(entryCheckpoint);
      return false;
    } else {
      return parse_group(ctx);
    }
  }

  template <ParseModeContext Context>
  bool parse_group(Context &ctx) const {
    const auto groupCheckpoint = ctx.mark();
    ProcessedFlags matchedFlags{};
    std::size_t matchedCount = 0;

    while (matchedCount < sizeof...(Elements)) {
      const auto betweenCheckpoint = ctx.mark();
      if (matchedCount > 0) {
        ctx.skip();
      }

      if (!try_match_one(ctx, matchedFlags)) {
        if (matchedCount > 0) {
          ctx.rewind(betweenCheckpoint);
        }
        break;
      }
      if constexpr (ExpectParseModeContext<Context>) {
        if (ctx.frontierBlocked()) {
          return true;
        }
      }
      ++matchedCount;
    }
    if (matchedCount == sizeof...(Elements)) {
      return true;
    }
    ctx.rewind(groupCheckpoint);
    return false;
  }

public:

  constexpr const char *terminal(const char *begin) const noexcept
    requires(... && TerminalCapableExpression<Elements>)
  {
    const char *cursor = begin;
    ProcessedFlags matchedFlags{};

    while (true) {
      bool matchedAnyElement = false;
      std::size_t elementIndex = 0;
      std::apply(
          [this, &cursor, &matchedFlags, &elementIndex, &matchedAnyElement](
              const auto &...element) {
            ((matchedAnyElement |=
              terminal_impl(element, cursor, matchedFlags, elementIndex++)),
             ...);
          },
          elements);

      if (!matchedAnyElement)
        break;
    }

    return std::ranges::all_of(matchedFlags, [](bool isMatched) {
             return isMatched;
           })
               ? cursor
               : nullptr;
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
    return SkipperWrapped<UnorderedGroup<Elements...>>{
        *this, static_cast<Skipper>(std::forward<LocalSkipper>(localSkipper))};
  }

  template <std::convertible_to<Skipper> LocalSkipper>
  auto with_skipper(LocalSkipper &&localSkipper) && {
    return SkipperWrapped<UnorderedGroup<Elements...>>{
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
  using ProcessedFlags = std::array<bool, sizeof...(Elements)>;
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
    using AccessorFn = const AbstractElement *(*)(const UnorderedGroup *) noexcept;

    static constexpr std::array<AccessorFn, sizeof...(Elements)> accessors = {
        +[](const UnorderedGroup *self) noexcept -> const AbstractElement * {
          return std::addressof(std::get<Is>(self->elements));
        }...};

    return accessors[elementIndex](this);
  }

  template <ParseModeContext Context, typename T>
  static bool try_match_rule_element(const T &element,
                                     ProcessedFlags &matchedFlags,
                                     Context &ctx,
                                     std::size_t elementIndex) {
    if (matchedFlags[elementIndex]) {
      return false;
    }
    if constexpr (StrictParseModeContext<Context>) {
      if (attempt_parse_strict(ctx, element)) {
        matchedFlags[elementIndex] = true;
        return true;
      }
    } else if constexpr (RecoveryParseModeContext<Context>) {
      if (attempt_parse_editable(ctx, element)) {
        matchedFlags[elementIndex] = true;
        return true;
      }
    } else {
      if (parse(element, ctx)) {
        matchedFlags[elementIndex] = !ctx.frontierBlocked();
        return true;
      }
    }
    return false;
  }

  template <ParseModeContext Context, std::size_t I = 0>
  bool try_match_one(Context &ctx, ProcessedFlags &matchedFlags) const {
    if constexpr (I == sizeof...(Elements)) {
      return false;
    } else {
      if constexpr (ExpectParseModeContext<Context>) {
        if (matchedFlags[I]) {
          return try_match_one<Context, I + 1>(ctx, matchedFlags);
        }
        const auto checkpoint = ctx.mark();
        ExpectBranchResult result{};
        collect_expect_branch(ctx, checkpoint, std::get<I>(elements), result);
        if (result.matched) {
          matchedFlags[I] = !result.blocked;
          return true;
        }
      } else if (try_match_rule_element(std::get<I>(elements), matchedFlags, ctx,
                                        I)) {
        return true;
      }
      return try_match_one<Context, I + 1>(ctx, matchedFlags);
    }
  }

  template <typename T>
  static constexpr bool terminal_impl(const T &element,
                                      const char *&cursor,
                                      ProcessedFlags &matchedFlags,
                                      std::size_t elementIndex) noexcept {
    if (matchedFlags[elementIndex])
      return false;

    if (const char *matchEnd = element.terminal(cursor);
        matchEnd != nullptr) {
      cursor = matchEnd;
      matchedFlags[elementIndex] = true;
      return true;
    }
    return false;
  }

public:
  std::tuple<Elements...> elements;
};

template <Expression... Elements>
using UnorderedGroupWithSkipper = SkipperWrapped<UnorderedGroup<Elements...>>;

namespace detail {

template <typename T> struct IsUnorderedGroupFlattenRaw : std::false_type {};

template <typename... E>
struct IsUnorderedGroupFlattenRaw<UnorderedGroup<E...>> : std::true_type {};

template <typename G>
  requires IsUnorderedGroupFlattenRaw<std::remove_cvref_t<G>>::value
constexpr decltype(auto) as_unordered_group_tuple(G &&group) {
  return std::forward<G>(group).elements;
}

template <Expression Expr>
  requires(!IsUnorderedGroupFlattenRaw<std::remove_cvref_t<Expr>>::value)
constexpr auto as_unordered_group_tuple(Expr &&expr) {
  return std::tuple<ExpressionHolder<Expr>>{std::forward<Expr>(expr)};
}

template <typename... Ts>
constexpr auto make_unordered_group(std::tuple<Ts...> &&elements) {
  return UnorderedGroup<Ts...>{std::move(elements)};
}

} // namespace detail

template <Expression Lhs, Expression Rhs>
constexpr auto operator&(Lhs &&lhs, Rhs &&rhs) {
  return detail::make_unordered_group(std::tuple_cat(
      detail::as_unordered_group_tuple(std::forward<Lhs>(lhs)),
      detail::as_unordered_group_tuple(std::forward<Rhs>(rhs))));
}

} // namespace pegium::parser
