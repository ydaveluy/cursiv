// Why a lowering or a token graph refused a grammar, the first refusal it
// recorded, and the sentence both generators print.
#pragma once

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

namespace detail {

// A successful empty derivation needs a finite proof; a cycle alone is not one.
template <class Visit>
void settle_empty(Visit&& visit) {
  for (bool changed = true; changed;) {
    changed = false;
    visit([&](auto& value, bool empty) {
      if (empty && !value) {
        value = 1;
        changed = true;
      }
    });
  }
}

}  // namespace detail

enum class LowerFail : std::uint8_t {
  none = 0,
  wordRefusedInLoop,
  rescuedRefusalPastMatch,
  wordPastMatch,
  boundaryDisagrees,
  regionBoundaryShared,
  sectionPointShared,
  sectionEndShared,
  sectionIndented,
  namePool,
  tooManyTerminals,
  tooManyRules,
  tooManyConfigurations,
  tooManyProductions,
  productionTooWide,
  tooManyPositions,
  tokenGraphTooLarge,
  tableTooLarge,
  echoUnmarked,
  indentShape,
  gapShape,
  gapTooMany,
  gapLeafPool,
  indentTwice,
  refusalIntoRelay,
  rescueReadsPast,
  boundaryMisses,
  unorderedTooWide,
  terminalCycle,
  terminalUnproductive,
  terminalNullableRepetition,
  terminalEmpty,
  terminalReadsRule,
  commitPrefixEmpty,
  triviaTied,
};

// The widest unordered group either engine spells, from what the descent, the
// dearer of the two, holds in 16 bits: three positions for every item of every
// subset, or two for every item of every order where an item nests.
constexpr unsigned unordered_most(bool nests) {
  const auto cost = [nests](std::uint64_t k) {
    if (!nests)
      return 3 * k * (std::uint64_t{1} << (k - 1)) + (std::uint64_t{1} << k);
    std::uint64_t orders = 1;
    for (std::uint64_t i = 2; i <= k; ++i) orders *= i;
    return orders * (2 * k + 1) + 1;
  };
  unsigned most = 1;
  while (cost(most + 1) <= kNoRow) ++most;
  return most;
}
// The sentence of `unorderedTooWide` states both.
static_assert(unordered_most(false) == 11 && unordered_most(true) == 6);

// `{bytes}` stands for the bytes a refusal turns on, `{word}` for the word.
constexpr std::string_view lower_fail_text(LowerFail f) {
  switch (f) {
    case LowerFail::wordRefusedInLoop:
      return "has a guard inside a repetition, and a turn of that repetition "
             "may read nothing after the guard -- make each turn read "
             "something after it, as in `!lit<\"/\"> + any`";
    case LowerFail::rescuedRefusalPastMatch:
      return "may end a match exactly where a word its guard excludes ends, "
             "after the start of the terminal, and {bytes} after that word "
             "lets the guard pass: whether the guard holds would depend on "
             "the character after the token, and at the end of the document "
             "there is none -- put the guard at the start of the terminal or "
             "after literals and character classes only, or let the terminal "
             "read {bytes} after the word";
    case LowerFail::wordPastMatch:
      return "may end a match before the end of {word}, which a guard "
             "excludes after the start of the terminal: whether the guard "
             "holds would depend on text after the token -- give the guarded "
             "literal a terminal of its own, put the guard at the start of "
             "the terminal or after literals and character classes only, or "
             "let the terminal read the whole word before it can end";
    case LowerFail::boundaryDisagrees:
      return "reaches the same match through two guards that exclude "
             "different characters after it, and the LR parser tests one set "
             "of characters after a match -- use the same guard on both paths";
    case LowerFail::regionBoundaryShared:
      return "reaches the same token both inside a `skip()` region and "
             "outside it, so the trivia before that token could follow either "
             "setting -- make the alternatives differ before the region "
             "opens, give the region its own opening token, or end the region "
             "on a token that no optional part follows";
    case LowerFail::sectionPointShared:
      return "reaches the same token both inside a `section<>` and outside "
             "it, so the trivia before that token could follow either grammar "
             "-- make the alternatives differ before the section opens";
    case LowerFail::sectionEndShared:
      return "closes a `section<>` on a token that the embedded grammar also "
             "reads, or that starts one of its tokens, so the section could "
             "end too early -- close it on a token that no token of the "
             "embedded grammar begins with";
    case LowerFail::sectionIndented:
      return "uses indentation and embeds a grammar with `section<>`: the "
             "lines of the section would be measured as lines of the document "
             "-- use `section<>` only in a grammar without indentation";
    case LowerFail::namePool:
      return "has names whose characters add up to more than 65 535 -- use "
             "fewer or shorter names, or split the grammar";
    case LowerFail::tooManyTerminals:
      return "declares more than 32 767 terminals -- split the grammar";
    case LowerFail::tooManyRules:
      return "declares more than 32 767 rules, counting the rules the "
             "generator adds -- simplify the grammar, or split it";
    case LowerFail::tooManyConfigurations:
      return "uses more than 255 trivia settings: its own, one for each "
             "distinct `skip()` and one for each `section<>` -- let `skip()` "
             "regions share the same lists";
    case LowerFail::productionTooWide:
      return "has an alternative longer than 65 535 items once expanded, too "
             "long for the LR parser -- narrow the range of a counted "
             "repetition, or use `many` and check the count in validation";
    case LowerFail::tooManyProductions:
      return "has more than 65 535 alternatives once expanded, too many for "
             "the LR parser -- simplify the grammar, or split it";
    case LowerFail::tooManyPositions:
      return "is too large for the LL parser once expanded, more than 65 535 "
             "items: a counted repetition is copied once for each turn it may "
             "take, a choice once for each alternative -- narrow the range, "
             "or use `many` and check the count in validation";
    case LowerFail::tokenGraphTooLarge:
      return "has terminals too complex for the generated lexer: more than "
             "65 535 character sets, states or parts, or a repetition bound "
             "above 4 294 967 294 -- simplify the largest terminals, such as "
             "a long list of keywords or of Unicode ranges, or split them";
    case LowerFail::tableTooLarge:
      return "is too large for the LR parser tables: more than 65 534 states "
             "and condition sets, 65 536 completion entries, or 2^32 actions "
             "or table blocks -- simplify the grammar, or split it";
    case LowerFail::echoUnmarked:
      return "uses `same<>` where a `mark<>` with its tag is not set on every "
             "path before it -- set the mark before the `same<>` on every "
             "path of the same terminal: a mark in one branch of a choice, or "
             "in a loop that may run zero times, does not count";
    case LowerFail::gapShape:
      return "uses an expression in `gap::has` or `gap::is` that they cannot "
             "test: they take classes, literals, `any`, sequences, choices "
             "and repetitions, never a `through`";
    case LowerFail::gapTooMany:
      return "has more than 255 gap conditions, or more than 32 implied "
             "tokens that can be inserted at one position -- simplify the "
             "conditions, or split the grammar";
    case LowerFail::gapLeafPool:
      return "has too many distinct expressions in its conditions -- "
             "simplify the conditions, or split the grammar";
    case LowerFail::indentShape:
      return "uses `indent` or `dedent` as a part of its body: each must be a "
             "whole terminal, `terminal<indent>`";
    case LowerFail::indentTwice:
      return "is a second `indent` or `dedent` terminal: a grammar has at "
             "most one of each -- declare `terminal<indent>` once and "
             "`terminal<dedent>` once";
    case LowerFail::refusalIntoRelay:
      return "has a guard whose excluded word reaches into the text between a "
             "`mark<>` and its `same<>`, which the generator cannot check -- in "
             "a nested run, a guard may test only the first character of that "
             "text, and not as a keyword";
    case LowerFail::rescueReadsPast:
      return "excludes a word with a keyword guard, then may read after it a "
             "character outside `word_boundary`: whether the guard holds "
             "would depend on that character -- add to `word_boundary` every "
             "character the terminal reads after the word";
    case LowerFail::boundaryMisses:
      return "excludes {word} at its start, and can read {bytes} at the start "
             "of a word and again after {word}, but {bytes} is not in the "
             "word boundary: after {word}, {bytes} does not continue the "
             "word, so {word} is excluded and the text splits into two tokens "
             "-- add {bytes} to `word_boundary`, or to the guard of the "
             "excluded literal";
    case LowerFail::unorderedTooWide:
      return "has an unordered group with too many items: at most 11, or 6 "
             "when an item holds a `nest` -- use fewer items, or read them as "
             "a repetition of a choice and check in validation that each "
             "appears once";
    case LowerFail::terminalCycle:
      return "calls itself without consuming input: every recursive cycle "
             "must read at least one character before the next call";
    case LowerFail::terminalUnproductive:
      return "has a recursive call that can never finish a match -- add an "
             "alternative without the call, or make the call optional";
    case LowerFail::terminalNullableRepetition:
      return "repeats an item that may match nothing inside a recursive "
             "terminal -- make the item consume input";
    case LowerFail::terminalEmpty:
      return "matches the empty string -- give its body a part it must read";
    case LowerFail::terminalReadsRule:
      return "uses the rule {word} inside a terminal or a `through`: these "
             "read one token, so they cannot call a rule, even a data rule -- "
             "use the terminals the rule is made of, or call the rule from a "
             "rule";
    case LowerFail::commitPrefixEmpty:
      return "has a `commit` whose prefix may match nothing: such a prefix "
             "always matches, so the quantifier around it never stops -- give "
             "the prefix a part it must read";
    case LowerFail::triviaTied:
      return "is trivia that matches the same text as another trivia "
             "terminal, with the same length and category, and one of the two "
             "is `hidden`: which one the tree keeps would depend on the order "
             "of declaration -- put both in `ignored`, or make them match "
             "different texts";
    case LowerFail::none:
      break;
  }
  return "";
}

// A grammar refused in the author's own words, where the walk cannot go on.
[[noreturn]] inline void refuse_lowering(std::string_view text) {
  throw std::runtime_error(std::string(text));
}

// A capacity found where the walk cannot go on: the refusal by its code, with
// the sentence a generator prints for it.
struct LowerRefused : std::length_error {
  LowerFail why;
  explicit LowerRefused(LowerFail f)
      : std::length_error("the grammar " + std::string(lower_fail_text(f))),
        why(f) {}
};

// The first refusal, if any: its reason, the terminal or the rule it names,
// the bytes it turns on, and the word it turns on as a lexical node.
struct LowerVerdicts {
  LowerFail lowerWhy = LowerFail::none;
  std::uint16_t lowerTerm = kNoRow;
  std::uint16_t lowerRule = kNoRow;
  ByteSet lowerBytes{};
  std::uint16_t lowerWord = kNoRow;

  constexpr bool lowerFail() const { return lowerWhy != LowerFail::none; }
};

// The first refusal stands; a later one is a consequence.
constexpr void lower_refuse(LowerVerdicts& f, LowerFail why,
                            std::uint16_t term = kNoRow,
                            const ByteSet& bytes = {}) {
  if (f.lowerFail()) return;
  f.lowerWhy = why;
  f.lowerTerm = term;
  f.lowerBytes = bytes;
}

// ...or a refusal of one rule's shape.
constexpr void lower_refuse_rule(LowerVerdicts& f, LowerFail why,
                                 std::uint16_t rule) {
  if (f.lowerFail()) return;
  lower_refuse(f, why);
  f.lowerRule = rule;
}

// A byte as a message spells it: itself where it prints, else its hex.
inline std::string spell_byte(unsigned b) {
  if (b > 0x20 && b < 0x7f) return std::string(1, static_cast<char>(b));
  char out[8];
  std::snprintf(out, sizeof out, "\\x%02x", b);
  return out;
}

// The refusal as a generator states it: the terminal, the rule `rule` names
// or the grammar, then the sentence with its bytes and its word spelled.
template <class Cfg>
std::string lower_fail_sentence(const Cfg& c, std::string_view rule = {}) {
  std::string bytes;
  bool wide = false;
  for (unsigned b = 0; b < 256; ++b) {
    if (!c.lowerBytes.has(static_cast<unsigned char>(b))) continue;
    if (b >= 0x80) {
      wide = true;
      continue;
    }
    unsigned e = b;
    while (e + 1 < 0x80 && c.lowerBytes.has(static_cast<unsigned char>(e + 1)))
      ++e;
    bytes += spell_byte(b);
    if (e > b + 1) bytes += '-';
    if (e > b) bytes += spell_byte(e);
    b = e;
  }
  if (!bytes.empty()) bytes = '`' + bytes + '`';
  if (wide)
    bytes +=
        bytes.empty() ? "a character past ASCII" : " or a character past ASCII";
  // A word as the grammar spells it, else as its letters do: each letter's
  // lowest byte.
  std::string word = "a word";
  if (c.lowerWord < c.lexSpelled.size() && !c.lexSpelled[c.lowerWord].empty()) {
    word = '`' + c.lexSpelled[c.lowerWord] + '`';
  } else if (c.lowerWord != kNoRow) {
    const LexNode e = c.lex[c.lowerWord];
    word = "`";
    for (unsigned i = 0; i < e.min; ++i) {
      const ByteSet& s = c.sets[c.lexKid[e.b + i]];
      for (unsigned b = 0; b < 256; ++b)
        if (s.has(static_cast<unsigned char>(b))) {
          word += static_cast<char>(b);
          break;
        }
    }
    word += '`';
  }
  std::string text(lower_fail_text(c.lowerWhy));
  const auto fill = [&](std::string_view mark, const std::string& with) {
    for (auto at = text.find(mark); at != std::string::npos;
         at = text.find(mark, at + with.size()))
      text.replace(at, mark.size(), with);
  };
  fill("{bytes}", bytes);
  fill("{word}", word);
  if (c.lowerTerm != kNoRow)
    return "terminal `" + std::string(c.term_name(c.lowerTerm)) + "` " + text;
  if (c.lowerRule != kNoRow && !rule.empty())
    return "rule `" + std::string(rule) + "` " + text;
  return "the grammar " + text;
}

}  // namespace cursiv
