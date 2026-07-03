#pragma once

/// Parser terminal matching any complete UTF-8 code point.

#include <pegium/core/grammar/AnyCharacter.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <string>

namespace pegium::parser {

struct AnyCharacter final : grammar::AnyCharacter {
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = true;

  using type = std::string_view;
  std::string_view getRawValue(const CstNodeView &node) const noexcept {
    return getValue(node);
  }

  constexpr const char *terminal(const char *begin) const noexcept {
    return utils::consume_utf8_codepoint_if_complete(begin);
  }
  constexpr const char *terminal(const std::string &text) const noexcept {
    return terminal(text.c_str());
  }

  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

private:
  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;

  bool probe_impl(const ParseContext &ctx) const noexcept {
    return terminal(ctx.cursor()) != nullptr;
  }

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    const char *const matchedEnd = terminal(ctx.cursor());
    if constexpr (StrictParseModeContext<Context>) {
      if (matchedEnd != nullptr) [[likely]] {
        ctx.leaf(matchedEnd, this);
        return true;
      }
      return false;
    } else if constexpr (RecoveryParseModeContext<Context>) {
      if (matchedEnd != nullptr) [[likely]] {
        ctx.leaf(matchedEnd, this);
        return true;
      }
      if (!ctx.canEdit()) {
        return false;
      }
      return detail::recover_by_terminal_delete_scan(
          ctx,
          [this](const char *scanCursor) noexcept {
            return terminal(scanCursor);
          },
          [this, &ctx](const char *scanEnd) { ctx.leaf(scanEnd, this); });
    } else {
      if (ctx.reachedAnchor()) {
        return false;
      }
      if (matchedEnd != nullptr && ctx.canTraverseUntil(matchedEnd)) {
        ctx.leaf(matchedEnd, this);
        return true;
      }
      if (!ctx.canEdit()) {
        return false;
      }
      return detail::recover_by_terminal_delete_scan(
          ctx,
          [this, &ctx](const char *scanCursor) noexcept -> const char * {
            const auto *scanEnd = terminal(scanCursor);
            return detail::can_apply_recovery_match(ctx, scanEnd)
                       ? scanEnd
                       : nullptr;
          },
          [this, &ctx](const char *scanEnd) { ctx.leaf(scanEnd, this); });
    }
  }
};

namespace detail {

template <> struct IsTerminalAtom<AnyCharacter> : std::true_type {};

} // namespace detail
} // namespace pegium::parser
