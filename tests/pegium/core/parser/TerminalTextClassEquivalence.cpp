#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <pegium/core/parser/TerminalTextClass.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <string>
#include <string_view>

// Differential proof for the M04 case-fold + decode-classify unification:
// pins the new shared header's behaviour against local, byte-for-byte
// reproductions of the three case-fold implementations and the two
// decode-classify walkers it replaces, run BEFORE any real call site is
// rewired to use it.

using namespace pegium::parser::detail;

namespace {

// --- Reference reproductions of the pre-unification implementations -------

// `normalize_char` (LiteralFuzzyMatcher.cpp) / the byte-level fold `Literal`
// already used via `pegium::utils::tolower`: identity outside 'A'-'Z'.
[[nodiscard]] constexpr unsigned char
reference_tolower_fold(unsigned char value, bool caseSensitive) noexcept {
  return caseSensitive ? value
                       : static_cast<unsigned char>(
                             pegium::utils::tolower(static_cast<char>(value)));
}

// The inline fold lambda from `short_literal_fuzzy_candidate_possible`
// (TerminalRecoverySupport.hpp): sets bit 0x20 on any byte that doesn't
// already carry it, rather than calling `tolower`.
[[nodiscard]] constexpr unsigned char
reference_bitor_fold(unsigned char value, bool caseSensitive) noexcept {
  if (caseSensitive || value >= 0x80u) {
    return value;
  }
  return static_cast<unsigned char>(value | 0x20u);
}

// `is_word_like_terminal` (ContextShared.hpp, pre-unification body): decodes
// UTF-8 across the whole string, returns false on the first non-identifier
// codepoint AND on truncated/invalid UTF-8 (truncation reads as a violation).
[[nodiscard]] constexpr bool
reference_is_word_like_terminal(std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }
  const char *cursor = value.data();
  const char *const end = cursor + value.size();
  while (cursor < end) {
    const auto length = pegium::utils::utf8_codepoint_length(*cursor);
    if (length == 0 || length > static_cast<std::size_t>(end - cursor)) {
      return false;
    }
    if (!is_identifier_like_codepoint(
            pegium::utils::decode_utf8_codepoint(cursor))) {
      return false;
    }
    cursor += length;
  }
  return true;
}

// `input_window_has_non_identifier_codepoint` (LiteralFuzzyMatcher.cpp,
// pre-unification body): decodes UTF-8 over a byte-bounded window, returns
// true on the first non-identifier codepoint, but returns false (not a
// violation) on truncated/invalid UTF-8 — the opposite truncation reading
// from the function above.
[[nodiscard]] constexpr bool
reference_input_window_has_non_identifier_codepoint(
    std::string_view input, std::size_t windowBytes) noexcept {
  const char *cursor = input.data();
  const char *const end = cursor + std::min(input.size(), windowBytes);
  while (cursor < end) {
    const auto length = pegium::utils::utf8_codepoint_length(*cursor);
    if (length == 0 || length > static_cast<std::size_t>(end - cursor)) {
      return false;
    }
    if (!is_identifier_like_codepoint(
            pegium::utils::decode_utf8_codepoint(cursor))) {
      return true;
    }
    cursor += length;
  }
  return false;
}

} // namespace

// --- ascii_fold vs both pre-unification case-fold shapes -------------------

TEST(TerminalTextClassEquivalenceTest,
     AsciiFoldAgreesWithToLowerFoldForEveryByte) {
  // `ascii_fold` is defined in terms of `pegium::utils::tolower`, so this
  // must hold by construction for all 256 byte values under both
  // case-sensitivity settings; the loop pins that contract explicitly.
  for (int byteValue = 0; byteValue < 256; ++byteValue) {
    const auto byte = static_cast<unsigned char>(byteValue);
    for (const bool caseSensitive : {true, false}) {
      SCOPED_TRACE(::testing::Message()
                   << "byte=0x" << std::hex << byteValue
                   << " caseSensitive=" << caseSensitive);
      EXPECT_EQ(ascii_fold(byte, caseSensitive),
                reference_tolower_fold(byte, caseSensitive));
    }
  }
}

TEST(TerminalTextClassEquivalenceTest,
     ToLowerFoldDivergesFromBitOrFoldOnNonLetterBytesWithClearBit0x20) {
  // The two ORIGINAL implementations are not byte-identical below 0x80:
  // `tolower`-fold only ever changes 'A'-'Z' (0x41-0x5A), while `byte |
  // 0x20` also flips any OTHER sub-0x80 byte whose bit 0x20 is clear —
  // control characters (0x00-0x1F) and the punctuation immediately
  // surrounding the letter range ('@' = 0x40, and '[' '\' ']' '^' '_' =
  // 0x5B-0x5F). `reference_bitor_fold` short-circuits to identity for any
  // byte >= 0x80 (mirroring the original lambda's own `byte >= 0x80u`
  // guard), so the two shapes agree trivially there too. Bytes already
  // carrying bit 0x20 (0x20-0x3F, 0x60-0x7F) are untouched by `|0x20` and
  // agree trivially. This test locks in the exact divergent set instead of
  // asserting a blanket equality that would fail.
  std::size_t divergentCount = 0;
  for (int byteValue = 0; byteValue < 256; ++byteValue) {
    const auto byte = static_cast<unsigned char>(byteValue);
    const bool caseSensitive = false;
    const auto tolowerFolded = reference_tolower_fold(byte, caseSensitive);
    const auto bitorFolded = reference_bitor_fold(byte, caseSensitive);
    const bool isUpperAsciiLetter = byteValue >= 'A' && byteValue <= 'Z';
    const bool bit0x20Clear = (byteValue & 0x20) == 0;
    const bool expectedDivergence =
        byteValue < 0x80 && bit0x20Clear && !isUpperAsciiLetter;
    if (expectedDivergence) {
      ++divergentCount;
      EXPECT_NE(tolowerFolded, bitorFolded)
          << "byte=0x" << std::hex << byteValue;
    } else {
      EXPECT_EQ(tolowerFolded, bitorFolded)
          << "byte=0x" << std::hex << byteValue;
    }
  }
  // 0x00-0x1F (32) + '@' (1) + 0x5B-0x5F (5) = 38 divergent bytes out of 256.
  EXPECT_EQ(divergentCount, 38u);

  // Case-sensitive folding never transforms anything on either side, so the
  // two shapes agree everywhere once `caseSensitive` is true.
  for (int byteValue = 0; byteValue < 256; ++byteValue) {
    const auto byte = static_cast<unsigned char>(byteValue);
    EXPECT_EQ(reference_tolower_fold(byte, true),
              reference_bitor_fold(byte, true));
  }
}

TEST(TerminalTextClassEquivalenceTest,
     BitOrFoldIsASoundOverApproximationOfToLowerFoldEquivalence) {
  // `short_literal_fuzzy_candidate_possible` only ever uses its fold as a
  // pre-filter ("does literal share a byte with input"), gating whether the
  // exact Levenshtein DP (which matches codepoints via `equal_codepoint`,
  // i.e. the tolower-fold contract) runs at all. Swapping its `|0x20` fold
  // for `ascii_fold` is safe iff `|0x20`-equality is a SUPERSET of
  // tolower-equality: whenever the tolower-fold contract says two bytes are
  // interchangeable, the bit-or fold must agree too, so no candidate the
  // exact matcher could find is ever pre-filtered away. This holds trivially
  // (tolower only equates identical bytes or 'A'-'Z'/'a'-'z' pairs, and the
  // bit-or fold agrees with tolower on exactly those pairs — proven above),
  // so tightening the pre-filter to `ascii_fold` cannot introduce a false
  // negative relative to the pre-unification `|0x20` behaviour.
  for (int lhsValue = 0; lhsValue < 256; ++lhsValue) {
    for (int rhsValue = 0; rhsValue < 256; ++rhsValue) {
      const auto lhs = static_cast<unsigned char>(lhsValue);
      const auto rhs = static_cast<unsigned char>(rhsValue);
      const bool toLowerEqual = reference_tolower_fold(lhs, false) ==
                                reference_tolower_fold(rhs, false);
      const bool bitOrEqual =
          reference_bitor_fold(lhs, false) == reference_bitor_fold(rhs, false);
      if (toLowerEqual) {
        EXPECT_TRUE(bitOrEqual) << "lhs=0x" << std::hex << lhsValue
                                << " rhs=0x" << rhsValue;
      }
    }
  }
}

// --- decode_classify_walk vs both pre-unification walkers -------------------

namespace {
struct DecodeClassifyCase {
  const char *name;
  std::string_view text;
};

// A representative spread: empty, ASCII word/non-word, valid multi-byte
// Unicode, truncated multi-byte tails, and invalid lead/continuation bytes.
constexpr DecodeClassifyCase kDecodeClassifyCases[] = {
    {"empty", ""},
    {"ascii word", "module"},
    {"ascii non-word first char", "!module"},
    {"ascii non-word last char", "module!"},
    {"ascii single char", "x"},
    {"valid two-byte codepoint (e-acute)", "caf\xC3\xA9"},
    {"valid three-byte codepoint (CJK)", "\xE9\x96\xA2\xE6\x95\xB0"},
    {"valid four-byte codepoint (emoji)", "\xF0\x9F\x98\x80"},
    {"truncated two-byte lead at end", "ab\xC3"},
    {"truncated three-byte lead at end", "ab\xE9\x96"},
    {"truncated four-byte lead at end", "ab\xF0\x9F\x98"},
    {"bare continuation byte", "ab\x80"},
    {"invalid lead byte 0xFF", "ab\xFF"},
};
} // namespace

TEST(TerminalTextClassEquivalenceTest,
     DecodeClassifyWalkReproducesIsWordLikeTerminal) {
  for (const auto &c : kDecodeClassifyCases) {
    SCOPED_TRACE(c.name);
    const bool expected = reference_is_word_like_terminal(c.text);
    const bool actual =
        c.text.empty()
            ? false
            : decode_classify_walk(c.text, c.text.size(),
                                   /*returnOnNonIdentifier=*/false,
                                   /*returnOnTruncation=*/false);
    EXPECT_EQ(actual, expected);
    // The production wrapper must match too.
    EXPECT_EQ(is_word_like_terminal(c.text), expected);
  }
}

TEST(TerminalTextClassEquivalenceTest,
     DecodeClassifyWalkReproducesInputWindowHasNonIdentifierCodepoint) {
  for (const auto &c : kDecodeClassifyCases) {
    for (const std::size_t windowBytes :
         {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3},
          c.text.size()}) {
      SCOPED_TRACE(::testing::Message()
                   << c.name << " windowBytes=" << windowBytes);
      const bool expected =
          reference_input_window_has_non_identifier_codepoint(c.text,
                                                               windowBytes);
      const bool actual =
          decode_classify_walk(c.text, windowBytes,
                              /*returnOnNonIdentifier=*/true,
                              /*returnOnTruncation=*/false);
      EXPECT_EQ(actual, expected);
      // The production wrapper must match too.
      EXPECT_EQ(input_window_has_non_identifier_codepoint(c.text, windowBytes),
                expected);
    }
  }
}

TEST(TerminalTextClassEquivalenceTest,
     IsWordLikeTerminalAndInputWindowCheckDisagreeOnTruncationPolarity) {
  // Pins the documented divergence: on a truncated tail, `is_word_like_terminal`
  // treats it as a violation (word-likeness == false) while
  // `input_window_has_non_identifier_codepoint` treats it as "no violation
  // found" (also returns false, but that means "identifier-like" in its own
  // inverted polarity) — the SAME truncation event, opposite interpretations.
  constexpr std::string_view truncated = "ab\xC3";
  EXPECT_FALSE(is_word_like_terminal(truncated));
  EXPECT_FALSE(input_window_has_non_identifier_codepoint(truncated,
                                                         truncated.size()));
}

// --- constexpr / compile-time usability -------------------------------------

namespace {
// `Literal.hpp` evaluates `is_word_like_terminal` in a `static constexpr`
// specialization field; this mirrors that usage shape at namespace scope to
// prove the whole header stays usable in a constant-evaluation context.
static_assert(is_word_like_terminal("module"));
static_assert(!is_word_like_terminal("=>"));
static_assert(!is_word_like_terminal(""));
static_assert(ascii_fold('A', false) == 'a');
static_assert(ascii_fold('A', true) == 'A');
static_assert(equal_codepoint('A', 'a', false));
static_assert(!equal_codepoint('A', 'a', true));
static_assert(equal_decoded_codepoint(U'A', U'a', false));
} // namespace
