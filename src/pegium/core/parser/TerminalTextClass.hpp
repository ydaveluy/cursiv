#pragma once

/// Shared ASCII case-fold and UTF-8 decode-classify vocabulary used by the
/// terminal strict-match path and the recovery fuzzy matcher. Everything
/// here is `constexpr`: `Literal.hpp` evaluates `is_word_like_terminal` at
/// compile time to cache word-likeness per literal specialization, so this
/// header must stay usable in a constant-evaluation context end to end.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <pegium/core/utils/TextUtils.hpp>
#include <string_view>

namespace pegium::parser::detail {

// The identifier-codepoint classifier lives in TextUtils so the parser,
// recovery and editor-facing (token boundary) code share one definition.
using utils::is_identifier_like_codepoint;
using utils::is_identifier_like_codepoint_at;

/// ASCII case-fold for one raw byte: identity when `caseSensitive`, otherwise
/// `pegium::utils::tolower`'s uppercase-to-lowercase mapping. Bytes outside
/// 'A'-'Z' — control characters, punctuation neighbouring the letter range
/// ('@', '[' through '_'), and non-ASCII lead/continuation bytes (>= 0x80) —
/// pass through unchanged, so callers that fold raw text bytes (including
/// punctuation and digits, not just letters) get the same identity-outside-
/// A-Z contract as the codepoint-level folding below.
[[nodiscard]] constexpr unsigned char ascii_fold(unsigned char byte,
                                                 bool caseSensitive) noexcept {
  return caseSensitive ? byte
                       : static_cast<unsigned char>(
                             utils::tolower(static_cast<char>(byte)));
}

[[nodiscard]] constexpr bool
equal_codepoint(unsigned char lhs, unsigned char rhs,
               bool caseSensitive) noexcept {
  return ascii_fold(lhs, caseSensitive) == ascii_fold(rhs, caseSensitive);
}

/// Case-folds a decoded codepoint. Only ASCII A-Z are folded (matching
/// `ascii_fold`, itself a no-op outside A-Z); non-ASCII codepoints pass
/// through unchanged so the codepoint DP preserves the exact ASCII
/// case-insensitivity contract while comparing whole codepoints.
[[nodiscard]] constexpr std::uint32_t
normalize_codepoint(std::uint32_t value, bool caseSensitive) noexcept {
  if (caseSensitive || value >= 0x80u) {
    return value;
  }
  return static_cast<std::uint32_t>(
      ascii_fold(static_cast<unsigned char>(value), caseSensitive));
}

[[nodiscard]] constexpr bool
equal_decoded_codepoint(std::uint32_t lhs, std::uint32_t rhs,
                        bool caseSensitive) noexcept {
  return normalize_codepoint(lhs, caseSensitive) ==
         normalize_codepoint(rhs, caseSensitive);
}

/// Bounds-checked decode-and-classify walk shared by `is_word_like_terminal`
/// and `input_window_has_non_identifier_codepoint` below. Scans `text` up to
/// `windowBytes` bytes (clipped to `text.size()`), decoding one UTF-8
/// codepoint per step:
///  - returns `returnOnNonIdentifier` as soon as a codepoint fails
///    `is_identifier_like_codepoint`;
///  - returns `returnOnTruncation` if the lead byte's declared length runs
///    past the scanned window (truncated / invalid encoding);
///  - returns `!returnOnNonIdentifier` once the whole window is scanned
///    without either outcome firing.
/// Both outcomes are caller-supplied parameters rather than baked into the
/// loop because the two callers disagree on which value truncation should
/// produce — see each wrapper's own contract below.
[[nodiscard]] constexpr bool
decode_classify_walk(std::string_view text, std::size_t windowBytes,
                     bool returnOnNonIdentifier,
                     bool returnOnTruncation) noexcept {
  const char *cursor = text.data();
  const char *const end = cursor + std::min(text.size(), windowBytes);
  while (cursor < end) {
    const auto length = utils::utf8_codepoint_length(*cursor);
    if (length == 0 || length > static_cast<std::size_t>(end - cursor)) {
      return returnOnTruncation;
    }
    if (!is_identifier_like_codepoint(utils::decode_utf8_codepoint(cursor))) {
      return returnOnNonIdentifier;
    }
    cursor += length;
  }
  return !returnOnNonIdentifier;
}

/// True iff every codepoint of `value` classifies as identifier-like (ASCII
/// word characters or any codepoint >= 0x80); false for an empty string, for
/// any string containing a non-identifier-like codepoint, and for truncated
/// / invalid UTF-8 (conservative: assume not-word-like). Caches per literal
/// specialization on the strict match hot path via a `static constexpr`
/// field in `Literal.hpp` — must stay constexpr.
[[nodiscard]] constexpr bool
is_word_like_terminal(std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }
  return decode_classify_walk(value, value.size(),
                              /*returnOnNonIdentifier=*/false,
                              /*returnOnTruncation=*/false);
}

/// True iff the first `windowBytes` bytes of `input` (clipped to its size)
/// contain a codepoint that is not identifier-like. Truncated / invalid
/// UTF-8 inside the inspected window reads as "no violation" (returns
/// false): invalid bytes can't be decoded into a codepoint anyway, so the
/// caller defers to the literal-vs-input shared-codepoint scan downstream —
/// keeping the same "treat as identifier-like" heuristic the byte-level
/// predecessor applied so adversarial inputs don't lose the matcher
/// fast-path.
[[nodiscard]] constexpr bool
input_window_has_non_identifier_codepoint(std::string_view input,
                                          std::size_t windowBytes) noexcept {
  return decode_classify_walk(input, windowBytes,
                              /*returnOnNonIdentifier=*/true,
                              /*returnOnTruncation=*/false);
}

} // namespace pegium::parser::detail
