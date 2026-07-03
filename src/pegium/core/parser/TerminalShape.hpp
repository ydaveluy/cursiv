#pragma once

/// `TerminalShape`: the closed lexical profile a terminal recovery
/// step reads to admit `Replace`, `Insert`, or `DeleteScan`
/// candidates.
///
/// The shape names three concrete properties of the terminal that an
/// admission rule can reason about:
///
///   - `hasCanonicalText`        the terminal has a canonical
///                               literal value (e.g. a keyword);
///                               required for `Replace`.
///   - `canonicalTextLength`     length in CODEPOINTS of the canonical
///                               text; feeds the fuzzy lookahead
///                               window calculation and the codepoint-
///                               granular admission gates. For ASCII
///                               keywords this equals the byte length.
///   - `singleCodepoint`         true iff the canonical text is a
///                               single codepoint; single-codepoint
///                               replacements must stay conservative.
///
/// Strict-path discipline: this header is recovery-side. It must
/// never be constructed on the strict-only nominal path.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <pegium/core/utils/TextUtils.hpp>

namespace pegium::parser::detail {

inline constexpr std::uint32_t kMaxScopedSyntheticInsertCanonicalTextLength = 2;

/// The closed lexical shape of a terminal.
struct TerminalShape {
  bool hasCanonicalText = false;
  std::uint32_t canonicalTextLength = 0;
  bool singleCodepoint = false;

  /// Replace requires multi-codepoint canonical text —
  /// single-codepoint replacements stay conservative and go through
  /// stricter paths.
  [[nodiscard]] constexpr bool allowsReplace() const noexcept {
    return hasCanonicalText && !singleCodepoint;
  }

  /// Insert requires a single-codepoint canonical text. Wider compact
  /// punctuation can be admitted by a caller-owned scoped rule, but it is
  /// not part of the terminal's global insert capability.
  [[nodiscard]] constexpr bool allowsInsert() const noexcept {
    return hasCanonicalText && singleCodepoint;
  }

  [[nodiscard]] friend bool
  operator==(const TerminalShape &a, const TerminalShape &b) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<TerminalShape>);
static_assert(sizeof(TerminalShape) <= 12);

/// Builds a `TerminalShape` from a literal value. Conservative on
/// empty input (returns the all-default shape).
[[nodiscard]] constexpr TerminalShape
make_terminal_shape_from_literal(std::string_view canonicalText) noexcept {
  if (canonicalText.empty()) {
    return TerminalShape{};
  }
  TerminalShape shape;
  shape.hasCanonicalText = true;
  // CODEPOINT count, not byte size: the fuzzy admission gates that read
  // `canonicalTextLength` (the <=2 short path, the >=5 long path, the
  // half-length floor and the rank limit) must bucket multibyte keywords by
  // codepoints so they stay in lockstep with the codepoint-granular DP. For
  // ASCII keywords this is identical to `canonicalText.size()`.
  shape.canonicalTextLength = static_cast<std::uint32_t>(
      pegium::utils::utf8_codepoint_count(canonicalText));
  // `singleCodepoint` must compare codepoint count, not byte count: a
  // multi-byte UTF-8 single codepoint (`é`, `→`, …) is still a single
  // codepoint and should retain `allowsInsert()` semantics. `.front()` is
  // safe because the early empty-return above guarantees non-emptiness.
  shape.singleCodepoint =
      pegium::utils::utf8_codepoint_length(canonicalText.front()) ==
      canonicalText.size();
  return shape;
}

/// Computes the fuzzy lookahead window:
///
///     maxLookahead = canonicalTextLength + affordableDeleteSpan
///
/// `affordableDeleteSpan` is supplied by the caller (typically the
/// remaining recovery budget translated to a byte span). Returning
/// 0 when the shape has no canonical text reflects the rule:
/// `Replace` is not admissible without a canonical text, so a
/// fuzzy window of 0 is correct.
[[nodiscard]] constexpr std::uint32_t
compute_terminal_max_lookahead(const TerminalShape &shape,
                                std::uint32_t affordableDeleteSpan) noexcept {
  if (!shape.hasCanonicalText) {
    return 0;
  }
  return shape.canonicalTextLength + affordableDeleteSpan;
}

} // namespace pegium::parser::detail
