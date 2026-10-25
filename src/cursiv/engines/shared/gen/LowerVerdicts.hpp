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
static_assert(unordered_most(false) == 11 && unordered_most(true) == 6);

// `{bytes}` stands for the bytes a refusal turns on, `{word}` for the word.
constexpr std::string_view lower_fail_text(LowerFail f) {
  switch (f) {
    case LowerFail::wordRefusedInLoop:
      return "refuses a word inside a repetition that may come back to a "
             "refusal without reading a byte: the token graph cannot spell "
             "the turn that follows the refusal -- give the turn a part it "
             "must read after the refusal";
    case LowerFail::rescuedRefusalPastMatch:
      return "ends a match exactly where the word it refuses ends, past its "
             "head, and {bytes} rescues that word: the frontier states which "
             "class the byte behind a match may NOT be, never which it must "
             "be -- and where the document ends there is no byte to rescue "
             "it with. Refuse the word at the token's head or behind literals "
             "and classes alone, or let the body read on over {bytes}";
    case LowerFail::wordPastMatch:
      return "may end a match short of {word}, a word it refuses past its "
             "head: whether {word} stands would turn on text past the token, "
             "and the token graph reads one class there, of bytes or of "
             "scalars, no more. Refuse it at the token's head or behind "
             "literals and classes alone, where it is read whole, or let the "
             "body read on to the word's end before it may stop";
    case LowerFail::boundaryDisagrees:
      return "reaches one match through two refusals of different classes: "
             "one class is tested after a match, not two";
    case LowerFail::regionBoundaryShared:
      return "reaches one point through a `skip()` region and outside it at "
             "once: the point that reads the next token cannot read the "
             "trivia in front of it two ways -- part the alternatives before "
             "the region opens, give the region an opening token of its own, "
             "or end it on one no optional part follows";
    case LowerFail::sectionPointShared:
      return "reaches one point inside a `section<>` and outside it at "
             "once: the point that reads the next token cannot read the "
             "trivia in front of it both as the embedded grammar's and as "
             "the caller's -- part the alternatives before the section opens";
    case LowerFail::sectionEndShared:
      return "closes a `section<>` on a token the embedded grammar "
             "reads, or on the start of one: where the section ends would be "
             "that grammar's reading, not the text's -- close it on a token "
             "none of the embedded grammar's begins with";
    case LowerFail::sectionIndented:
      return "reads its lines by indentation and embeds a grammar with "
             "`section<>`: the section's lines would be measured as the "
             "document's -- embed it in a grammar that states no indentation";
    case LowerFail::namePool:
      return "spells more names than the 16-bit name pool holds";
    case LowerFail::tooManyTerminals:
      return "declares more than 32 767 terminals";
    case LowerFail::tooManyRules:
      return "declares more than 32 767 rules, auxiliaries included";
    case LowerFail::tooManyConfigurations:
      return "reads under more than 255 skip configurations: the grammar's "
             "own, one for every distinct pair of lists a `skip()` names and "
             "one for every `section<>`, each named by a byte -- let "
             "regions share their lists";
    case LowerFail::productionTooWide:
      return "spells a right-hand side of more than 65 535 positions: a "
             "production's length is sixteen bits";
    case LowerFail::tooManyProductions:
      return "lowers to more than 65 535 productions";
    case LowerFail::tooManyPositions:
      return "lowers to more than 65 535 positions: a counted repetition has "
             "one for every turn it may take, a choice one for every arm -- "
             "narrow the range, or repeat with `many` and check the count "
             "where the tree is read";
    case LowerFail::tokenGraphTooLarge:
      return "spells a token graph past the indices of its tables: more than "
             "65 535 byte classes, tags, nodes, edges, relays, lexing modes, "
             "scalar ranges or states, or a repetition bound past 32 bits -- give "
             "a large alternative, such as a table of keywords or a set of "
             "scalar ranges, fewer or narrower terminals";
    case LowerFail::tableTooLarge:
      return "needs a table past the indices of its program: more than "
             "65 534 states and guards together, more than 65 536 distinct "
             "completion entries, or more than 2^32 actions or matrix blocks "
             "-- split the grammar";
    case LowerFail::echoUnmarked:
      return "echoes a mark<> not set in its scope: same<> needs a mark "
             "with its tag before it on every path of the current terminal "
             "call or of the environment inherited by a lookahead";
    case LowerFail::gapShape:
      return "asks a condition of an expression the matcher does not read: "
             "`gap::has` and `gap::is` take classes, literals, `any`, "
             "sequences, choices and repetitions, never a `through`";
    case LowerFail::gapTooMany:
      return "states more than 255 gap conditions, or more implied tokens "
             "than a reading holds a bit for at one position";
    case LowerFail::gapLeafPool:
      return "asks more distinct questions in its conditions than the "
             "16-bit leaf pool holds";
    case LowerFail::indentShape:
      return "spells `indent` or `dedent` inside a body: each is a "
             "terminal's whole body, `terminal<indent>`";
    case LowerFail::indentTwice:
      return "declares two indent terminals, or two dedent terminals: the "
             "indentation answers one of each";
    case LowerFail::refusalIntoRelay:
      return "refuses a word that runs into a relay's content: the relay "
             "reads it by search and no edge of the token graph narrows it. "
             "Only the content's first byte can be refused, in a nested run, "
             "with no rescue class";
    case LowerFail::rescueReadsPast:
      return "reads on past a word it refuses over a character its word "
             "boundary does not hold: whether the word stands turns on that "
             "character, which the token graph reads byte by byte -- let the "
             "word boundary hold every character the terminal reads after "
             "the word";
    case LowerFail::boundaryMisses:
      return "refuses {word} at its head, and reads {bytes} where a word of "
             "its own begins and again past {word}, yet the class that "
             "rescues {word} leaves {bytes} out: there {word} stands refused "
             "and the word splits in two tokens -- let the word boundary, or "
             "the refused literal's guard, hold {bytes}";
    case LowerFail::unorderedTooWide:
      return "holds an unordered group of too many items: `a & b & ...` is "
             "spelled once for every subset of its items, and once for every "
             "order of them where an item nests, and those outgrow the 16-bit "
             "pools of the program -- group fewer items, or read them as a "
             "repetition of a choice and check each was read once where the "
             "tree is read";
    case LowerFail::terminalCycle:
      return "calls itself without consuming input: every recursive cycle "
             "must read at least one byte before the next call";
    case LowerFail::terminalUnproductive:
      return "has a recursive call that cannot return a finite match: give "
             "the recursion a terminating alternative";
    case LowerFail::terminalNullableRepetition:
      return "repeats an item that may match nothing inside a recursive "
             "terminal: make the item consume input";
    case LowerFail::terminalEmpty:
      return "matches the empty string -- give its body a part it must read";
    case LowerFail::terminalReadsRule:
      return "reads the rule {word}: a rule is read with the skipper between "
             "its parts, a phrase of tokens and never the bytes of one -- "
             "name the terminals it is made of, or read the rule from a rule";
    case LowerFail::commitPrefixEmpty:
      return "commits on a prefix that may read nothing: its quantifier "
             "stops where the prefix cannot be read, and a prefix that reads "
             "nothing is always there -- give the prefix a part it must read";
    case LowerFail::triviaTied:
      return "is trivia that reads the same text as another trivia terminal, "
             "at the same length and rank, and one of the two is hidden: "
             "which of them the tree keeps would turn on the order they are "
             "declared in -- put both in `ignore`, or make them read "
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
