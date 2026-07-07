#pragma once
/// Parser implementation of positive lookahead predicates.
#include <memory>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ExpectFrontier.hpp>
#include <pegium/core/grammar/AndPredicate.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <string>
#include <string_view>

namespace pegium::parser {

template <Expression Element>
struct AndPredicate final : grammar::AndPredicate {
  static constexpr bool nullable = true;
  static constexpr bool isFailureSafe = true;

  explicit constexpr AndPredicate(Element &&element)
    requires(!std::is_lvalue_reference_v<Element>)
      : _element(std::move(element)) {}
  explicit constexpr AndPredicate(Element element)
    requires(std::is_lvalue_reference_v<Element>)
      : _element(element) {}

  constexpr AndPredicate(AndPredicate &&) noexcept = default;
  constexpr AndPredicate(const AndPredicate &) = default;
  constexpr AndPredicate &operator=(AndPredicate &&) noexcept = default;
  constexpr AndPredicate &operator=(const AndPredicate &) = default;

  const AbstractElement *getElement() const noexcept override {
    return std::addressof(_element);
  }

  constexpr const char *terminal(const char *begin) const noexcept
    requires TerminalCapableExpression<Element>
  {
    return _element.terminal(begin) != nullptr ? begin : nullptr;
  }
  constexpr const char *terminal(const std::string &text) const noexcept
    requires TerminalCapableExpression<Element>
  {
    return terminal(text.c_str());
  }

  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

private:
  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;
  friend struct detail::InitAccess;

  ExpressionHolder<Element> _element;

  void init_impl(AstReflectionInitContext &ctx) const { parser::init(_element, ctx); }

  bool probe_impl(ParseContext &ctx) const {
    return parser::probe(_element, ctx);
  }

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (!ExpectParseModeContext<Context>) {
      if constexpr (RecoveryParseModeContext<Context>) {
        TrackedParseContext &strictCtx = ctx;
        return parser::probe(_element, strictCtx);
      } else {
        ParseContext &strictCtx = ctx;
        return parser::probe(_element, strictCtx);
      }
    } else {
      if (ctx.cursor() == ctx.end) {
        const auto checkpoint = ctx.mark();
        if (!parse(_element, ctx)) {
          ctx.rewind(checkpoint);
          return false;
        }
        auto branchFrontier = capture_frontier_since(ctx, checkpoint);
        ctx.rewind(checkpoint);
        if (branchFrontier.blocked && !branchFrontier.items.empty()) {
          merge_captured_frontier(ctx, branchFrontier, false);
        }
        return true;
      }

      const std::string_view remaining{
          ctx.cursor(), static_cast<std::size_t>(ctx.end - ctx.cursor())};
      auto probeCtx =
          ExpectContext{remaining, ctx.skipper(),
                        static_cast<TextOffset>(remaining.size())};
      probeCtx.wordChars = ctx.wordChars;
      probeCtx.setInRecoveryPhase(false);
      return with_no_edits(probeCtx, [this, &probeCtx]() {
        return parse(_element, probeCtx);
      });
    }
  }
};

template <Expression Element> constexpr auto operator&(Element &&element) {
  return AndPredicate<Element>{std::forward<Element>(element)};
}
} // namespace pegium::parser
