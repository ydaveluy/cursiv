#pragma once

/// Parse-mode concepts and dispatch helpers shared by parser expressions.

#include <concepts>
#include <type_traits>
#include <utility>

#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ParseContext.hpp>
namespace pegium::parser {

template <typename Context>
concept StrictParseModeContext =
    std::same_as<std::remove_cvref_t<Context>, ParseContext> ||
    std::same_as<std::remove_cvref_t<Context>, TrackedParseContext>;

template <typename Context>
concept RecoveryParseModeContext =
    std::same_as<std::remove_cvref_t<Context>, RecoveryContext>;

/// All four context types enter a combinator through one `parse_impl` template,
/// but expect mode (completion-frontier construction) is a genuinely different
/// algorithm per combinator, and that asymmetry is deliberate — do not "fix" it into
/// false uniformity:
///   - `Group` handles expect mode with its own dedicated, return-first-match
///     `parse_elements_expect` (matches "does this sequence match here?");
///   - `OrderedChoice` and `InfixRule` intentionally fork a *full-enumerate* expect
///     traversal (visit every branch / tail-operator to build the frontier —
///     `OrderedChoice::parse_expect_impl`, `InfixRule`'s expect fn-ptrs).
/// Enumerate-all vs return-first are different algorithms, not an accidental fork;
/// forcing them into one shared body would either bloat the strict/recovery callers
/// with expect-only enumeration or silently change completion behavior.
template <typename Context>
concept ExpectParseModeContext =
    std::same_as<std::remove_cvref_t<Context>, ExpectContext>;

template <typename Context>
concept ParseModeContext = StrictParseModeContext<Context> ||
                           RecoveryParseModeContext<Context> ||
                           ExpectParseModeContext<Context>;

template <typename Context>
concept EditableParseModeContext =
    RecoveryParseModeContext<Context> || ExpectParseModeContext<Context>;

namespace detail {

// When `PEGIUM_DISABLE_RECOVERY` is defined, every combinator's recovery-mode
// `parse`/`probe*` slot is compiled out (see the `if constexpr` gates in
// `ParseExpression.hpp`'s `Wrapper::Model::make_ops` and
// `InfixRule.hpp`'s `Model::make_ops`, plus the matching null-safe dispatch
// in `Wrapper::probe_match_here`/`parse_impl` and `InfixRule::probeRecoverable`/
// `parse_impl`): a grammar built this way never instantiates the
// recovery-mode template chain for any rule, at the cost of `parse()` always
// reporting the strict result for invalid input (equivalent to running with
// `ParseOptions::recoveryEnabled = false`).
#ifdef PEGIUM_DISABLE_RECOVERY
inline constexpr bool kRecoveryCompiledIn = false;
#else
inline constexpr bool kRecoveryCompiledIn = true;
#endif

template <typename Expr, typename Context>
concept HasParseImpl = requires(const Expr &expression, Context &ctx) {
  { expression.parse_impl(ctx) } -> std::same_as<bool>;
};

struct ParseAccess {
  template <typename Expr, ParseModeContext Context>
    requires HasParseImpl<Expr, Context>
  static bool parse(const Expr &expression, Context &ctx) {
    return expression.parse_impl(ctx);
  }
};

template <typename Expr, typename Context>
concept ParseAccessAvailable =
    requires(const Expr &expression, Context &ctx) {
      { ParseAccess::parse(expression, ctx) } -> std::same_as<bool>;
    };

template <typename Expr, typename Context>
concept HasProbeImpl = requires(const Expr &expression, Context &ctx) {
  { expression.probe_impl(ctx) } -> std::same_as<bool>;
};

struct ProbeAccess {
  template <typename Expr, StrictParseModeContext Context>
    requires HasProbeImpl<Expr, Context>
  static bool probe(const Expr &expression, Context &ctx) {
    return expression.probe_impl(ctx);
  }

  template <typename Expr, StrictParseModeContext Context>
    requires(!HasProbeImpl<Expr, Context> &&
             ParseAccessAvailable<Expr, Context>)
  static bool probe(const Expr &expression, Context &ctx) {
    const auto checkpoint = ctx.mark();
    if constexpr (requires { ctx.restoreMaxCursor(ctx.maxCursor()); }) {
      // Save & restore the max cursor across a probe so the probe leaves no
      // observable trace on contexts that track it (TrackedParseContext,
      // ExpectContext). The strict-only `ParseContext` does not track a
      // separate max cursor and therefore takes the cheaper rewind-only
      // branch.
      const auto maxCursor = ctx.maxCursor();
      const bool matched = ParseAccess::parse(expression, ctx);
      ctx.rewind(checkpoint);
      ctx.restoreMaxCursor(maxCursor);
      return matched;
    } else {
      const bool matched = ParseAccess::parse(expression, ctx);
      ctx.rewind(checkpoint);
      return matched;
    }
  }
};

template <typename Expr, typename Context>
concept ProbeAccessAvailable =
    requires(const Expr &expression, Context &ctx) {
      { ProbeAccess::probe(expression, ctx) } -> std::same_as<bool>;
    };

} // namespace detail

template <typename Expr, ParseModeContext Context>
  requires detail::ParseAccessAvailable<Expr, Context>
inline bool parse(const Expr &expression, Context &ctx) {
  return detail::ParseAccess::parse(expression, ctx);
}

template <typename Expr, StrictParseModeContext Context>
  requires detail::ProbeAccessAvailable<Expr, Context>
inline bool probe(const Expr &expression, Context &ctx) {
  return detail::ProbeAccess::probe(expression, ctx);
}

template <ParseModeContext Context, typename Fn>
inline decltype(auto) with_no_edits(Context &ctx, Fn &&fn) {
  if constexpr (EditableParseModeContext<Context>) {
    auto noEditGuard = ctx.withEditTrackingDisabled();
    (void)noEditGuard;
    return std::forward<Fn>(fn)();
  } else {
    return std::forward<Fn>(fn)();
  }
}

} // namespace pegium::parser
