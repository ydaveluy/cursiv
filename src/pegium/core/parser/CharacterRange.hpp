#pragma once

/// Parser terminal matching one byte against a compile-time character set.

#include <algorithm>
#include <array>
#include <pegium/core/grammar/CharacterRange.hpp>
#include <pegium/core/parser/AnyCharacter.hpp>
#include <pegium/core/parser/CompletionSupport.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/Group.hpp>
#include <pegium/core/parser/NotPredicate.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <ranges>
#include <string>

namespace pegium::parser {

template <auto range>
struct CharacterRange final : grammar::CharacterRange,
                              CompletionTerminalMatcher {
  static constexpr std::array<bool, 256> lookup =
      utils::createCharacterRange({range.data(), range.size()});
  static_assert(!lookup[0],
                "CharacterRange cannot include '\\0'.");
  const std::array<bool, 256> &characterMask() const noexcept override {
    return lookup;
  }
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = true;
  using type = std::string_view;
  std::string_view getRawValue(const CstNodeView &node) const noexcept {
    return getValue(node);
  }

  constexpr const char *terminal(const char *begin) const noexcept {

    return matches(begin) ? begin + 1 : nullptr;
  }
  constexpr const char *terminal(const std::string &text) const noexcept {
    return terminal(text.c_str());
  }

  [[nodiscard]] const char *
  matchForCompletion(const char *begin) const noexcept override {
    return terminal(begin);
  }

  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

  void print(std::ostream &os) const override {
    os << '[';
    std::size_t rangeStart = 0;
    bool hasRangeStart = false;
    for (std::size_t codepoint = 0; codepoint < 256; ++codepoint) {
      if (lookup[codepoint]) {
        if (!hasRangeStart) {
          rangeStart = codepoint;
          hasRangeStart = true;
        }
      } else {
        if (hasRangeStart) {
          if (codepoint - rangeStart == 1u)
            os << utils::escape_char(static_cast<char>(rangeStart));
          else if (codepoint - rangeStart == 2u)
            os << utils::escape_char(static_cast<char>(rangeStart))
               << utils::escape_char(static_cast<char>(rangeStart + 1));
          else
            os << utils::escape_char(static_cast<char>(rangeStart))
               << '-'
               << utils::escape_char(static_cast<char>(codepoint - 1));
          hasRangeStart = false;
        }
      }
    }
    if (hasRangeStart) {
      if (255u - rangeStart == 0u)
        os << utils::escape_char(static_cast<char>(rangeStart));
      else if (255u - rangeStart == 1u)
        os << utils::escape_char(static_cast<char>(rangeStart))
           << utils::escape_char(static_cast<char>(rangeStart + 1));
      else
        os << utils::escape_char(static_cast<char>(rangeStart))
           << '-'
           << utils::escape_char(static_cast<char>(255));
    }
    os << ']';
  }

private:
  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;

  bool probe_impl(const ParseContext &ctx) const noexcept {
    return matches(ctx.cursor());
  }

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      if (const char *const cursorStart = ctx.cursor(); matches(cursorStart)) {
        ctx.leaf(cursorStart + 1, this);
        return true;
      }
      return false;
    } else if constexpr (RecoveryParseModeContext<Context>) {
      if (const char *const cursorStart = ctx.cursor(); matches(cursorStart)) {
        ctx.leaf(cursorStart + 1, this);
        return true;
      }
      if (!ctx.canEdit()) {
        return false;
      }
      return detail::recover_by_terminal_delete_scan(
          ctx,
          [this](const char *scanCursor) noexcept {
            return matches(scanCursor) ? scanCursor + 1 : nullptr;
          },
          [this, &ctx](const char *matchedEnd) { ctx.leaf(matchedEnd, this); });
    } else {
      if (ctx.reachedAnchor()) {
        return false;
      }
      if (matches(ctx.cursor())) {
        ctx.leaf(ctx.cursor() + 1, this);
        return true;
      }
      if (!ctx.canEdit()) {
        return false;
      }
      return detail::recover_by_terminal_delete_scan(
          ctx,
          [this, &ctx](const char *scanCursor) noexcept -> const char * {
            const auto *matchedEnd =
                matches(scanCursor) ? scanCursor + 1 : nullptr;
            return detail::can_apply_recovery_match(ctx, matchedEnd)
                       ? matchedEnd
                       : nullptr;
          },
          [this, &ctx](const char *matchedEnd) { ctx.leaf(matchedEnd, this); });
    }
  }

  static constexpr bool matches(const char *cursor) noexcept {
    return lookup[static_cast<unsigned char>(*cursor)];
  }
};

namespace detail {

template <auto range>
struct IsTerminalAtom<CharacterRange<range>> : std::true_type {};

} // namespace detail

} // namespace pegium::parser
