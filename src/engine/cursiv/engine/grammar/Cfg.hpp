// A grammar lowered to productions: what the automaton is built from, and
// what each production's record carries.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engine/grammar/GrammarImage.hpp"
#include "cursiv/engine/grammar/GrammarWord.hpp"
#include "cursiv/engine/grammar/LowerDesc.hpp"
#include "cursiv/engine/grammar/Refused.hpp"
#include "cursiv/engine/parser/Tables.hpp"

namespace cursiv::engine::gen {

inline constexpr std::uint32_t kNone = ~std::uint32_t{0};

// A terminal below `kNonterminal`, a nonterminal's index above it.
using Symbol = std::uint32_t;
inline constexpr Symbol kNonterminal = 0x80000000u;
constexpr bool is_terminal(Symbol s) { return s < kNonterminal; }
constexpr Symbol nonterminal(std::uint32_t index) { return kNonterminal | index; }
constexpr std::uint32_t index_of(Symbol s) { return s & ~kNonterminal; }

struct Terminal {
  // The rule's name, or the literal between quotes.
  std::string name;
  // Whether a rule names it, and the comment written on it.
  bool named = false;
  std::string_view doc{};
  // What the lexer reads; none for the end of input.
  ImageId body = kNoImage;
  // A literal's spelling; empty for any other terminal.
  std::string_view text{};
  // The alternatives of a terminal made of words, in declaration order.
  std::vector<GrammarWord> words{};
  bool folded = false;
  // A literal written as a keyword, and whether it is then read only where
  // no word goes on past it.
  bool keyword = false;
  bool word = false;
  Trivia trivia = Trivia::none;
  // Where a trivia scope passes a terminal otherwise than the grammar: the
  // terminal this copy reads as; 0 for a terminal of the grammar's own.
  std::uint16_t twin = 0;
  // What its leaf carries: a named terminal's conversion, nothing for a
  // literal.
  std::uint32_t entry = kNone;

  [[nodiscard]] bool literal() const { return !text.empty(); }
  void each_word(auto&& visit) const {
    if (literal()) visit(GrammarWord{text, folded, doc});
    for (const GrammarWord& word : words) visit(word);
  }
};

// What must hold where a production is reduced, each part that is given.
struct Test {
  // What the token that comes must be: one of `ahead`, or with `unless`
  // none of them.
  std::vector<std::uint16_t> ahead;
  // One more than the condition of `Cfg::conditions` the text that comes
  // must read, or with `unless` must not; 0 for none.
  std::uint32_t condition = 0;
  bool unless = false;
  // A commit's exit tests the token as the lexer elected it, not its text.
  bool elected = false;
  // What the text between the last token and the one that comes must be,
  // or with `gapUnless` must not be: nothing, text that holds a line ending,
  // or text a part of which (`has`) or all of which (`is`) reads as the
  // pattern `gapPattern`, which monitor `gapMonitor` of the lexer reads.
  enum class Gap : std::uint8_t { any, empty, line, has, is };
  Gap gap = Gap::any;
  bool gapUnless = false;
  ImageId gapPattern = kNoImage;
  std::uint32_t gapMonitor = 0;
  // Of a test of the token that comes, the terminals whose tokens may stand
  // where its own alphabet, the tokens it names and the trivia, reads trivia
  // or a token it names: where one comes, the text is read again in mode
  // `alphabet`.
  std::vector<std::uint16_t> shadows;
  std::uint16_t alphabet = 0;
};

struct Production {
  std::uint32_t lhs = 0;
  // The declaring rule and innermost written alternative, for generator diagnostics.
  std::uint32_t rule = kNone;
  std::uint32_t alternative = 0;
  std::vector<Symbol> rhs;
  // The record it closes: an entry of `Cfg::entries`, none for a production
  // that writes nothing.
  std::uint32_t entry = kNone;
  // The tests one of which must hold for it to be reduced; none where it is
  // reduced wherever its symbols are read. The end of input written after a
  // production is such a test.
  std::vector<Test> tests;
  // One-based index of the conditions checked at the opening of this left recursion.
  std::uint32_t opening = 0;
  // Per place before a symbol or at the end, ascending, the terminals the
  // token that comes there may not be: what is reduced there, nothing read
  // since, is not reduced on them.
  std::vector<std::pair<std::uint32_t, std::vector<std::uint16_t>>> bars;
  // What a condition reads: whole once its symbols are read, whatever
  // follows.
  bool start = false;
  // An implied token left out: reduced only where its state has nothing
  // else to do with the token that comes.
  bool implied = false;
  [[nodiscard]] bool tested() const { return !tests.empty() || opening != 0; }
  // Whether its test `test` reads the text of the token in hand, not only
  // its terminal: another test may hold instead, or a shadow may come.
  [[nodiscard]] bool readsText(const Test& test) const {
    return !test.ahead.empty() && (tests.size() > 1 || !test.shadows.empty());
  }
  // Whether the token in hand may fail its tests: each asks more than the
  // state's actions do, the token's text, its gap or a condition.
  [[nodiscard]] bool checks() const {
    return opening != 0 || (!tests.empty() && std::ranges::all_of(tests, [&](const Test& test) {
             return readsText(test) || test.gap != Test::Gap::any || test.condition != 0;
           }));
  }
  // The trivia scope it was written in: what stands between its symbols;
  // per symbol, the scope of the gap before it, where they differ.
  std::uint16_t region = 0;
  std::vector<std::uint16_t> scopes;
  [[nodiscard]] std::uint16_t scope_at(std::size_t at) const {
    return scopes.empty() ? region : scopes[at];
  }
  // Its place among the productions of its nonterminal where two of them
  // read one text: the lowest is preferred.
  std::uint32_t rank = 0;
  // An unordered item built over the prefix to its left is elected from
  // that prefix onwards, like a turn of a left recursion.
  bool prefixTurn = false;
  // Per symbol, the entry its one record takes over, none where the record
  // keeps its own: empty where no symbol's does.
  std::vector<std::uint32_t> takes;
};

// A trivia scope: the terminals dropped between the tokens read in it, and
// those kept beside the tree; each list ascending. Its text ends at the end
// of input, or in a section at the token that closes it, its grammar's.
struct Region {
  std::vector<std::uint16_t> ignored;
  std::vector<std::uint16_t> hidden;
  std::uint16_t end = kEndOfInput;
  std::uint32_t grammar = kNone;
  // The tokens that may read the text of one of its trivia; ascending.
  std::vector<std::uint16_t> shadows;
  friend auto operator<=>(const Region&, const Region&) = default;
};

// More entries than the records of a grammar can need: one whose rules wrap
// one another in ever more ways stops here.
inline constexpr std::uint32_t kEntryLimit = 1u << 24;
// More symbols than a production written by hand holds: repetitions with a
// count, written out one inside the other, stop here.
inline constexpr std::size_t kProductionLimit = std::size_t{1} << 12;
// An action's number is 16 bits.
inline constexpr std::size_t kActionLimit = std::size_t{1} << 16;

// The levels of an infix rule. An operand that is whole when an operator
// comes is the left one of that operator or the right one of the operator
// before it: the levels say which holds.
struct Infix {
  struct Level {
    Association group = Association::Left;
    // The production that builds a node of this level, and the nonterminal
    // of the level's operators.
    std::uint32_t node = 0;
    std::uint32_t operators = 0;
  };
  // The tightest first.
  std::vector<Level> levels;
};

struct Cfg {
  // An entry is what one record carries: the actions of the records fused
  // into it, innermost first, each by its number among those the grammar
  // publishes. It is kept as the entry it extends, none for a first action,
  // then its last action: entries that begin alike share what they begin
  // with.
  struct Entry {
    std::uint32_t before = kNone;
    std::uint16_t action = 0;
  };

  // Terminal 0 is the end of input.
  std::vector<Terminal> terminals;
  // The trivia scopes; scope 0 is the grammar's own.
  std::vector<Region> regions;
  // The terminals indentation makes, 0 where the grammar reads none; the
  // brackets between which it says nothing, and the terminal that ends a
  // line, trivia between them.
  std::uint16_t indent = 0;
  std::uint16_t dedent = 0;
  std::vector<std::uint16_t> opens, closes;
  std::uint16_t newline = 0;
  Nesting nesting(std::uint16_t terminal) const {
    if (terminal == indent && terminal != 0) return Nesting::indent;
    if (terminal == dedent && terminal != 0) return Nesting::dedent;
    if (std::ranges::binary_search(opens, terminal)) return Nesting::open;
    if (std::ranges::binary_search(closes, terminal)) return Nesting::close;
    return Nesting::none;
  }

  // The terminal no text reads, which a section's conditions name where its
  // close stands; 0 where none does.
  std::uint16_t unread = 0;
  // How many nonterminals there are. Nonterminal 0 is the start; production
  // 0 reads the entry rule.
  std::uint32_t nonterminals = 0;
  struct RuleSite {
    std::uint32_t rule;
    std::uint32_t nt;
    std::uint16_t region;
  };
  std::vector<RuleSite> rules;
  // Each distinct flag expression and a rule that contains it.
  std::vector<std::pair<ImageId, std::uint32_t>> flags;
  std::vector<Production> productions;
  // Per nonterminal, its productions in order: written once lowering ends.
  std::vector<std::vector<std::uint32_t>> productionsOf;
  // The productions that read what a condition over several tokens tests.
  std::vector<std::uint32_t> conditions;
  // Per guarded reduction, its true markers and their depths below the left operand.
  std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> openings;
  std::vector<Infix> infixes;
  // Per data rule, the action that converts what it read and its nonterminal.
  std::vector<std::pair<std::uint16_t, std::uint32_t>> dataRules;
  std::vector<Entry> entries;
  // The actions an entry names: those the grammar publishes, then the texts
  // of the uses of words documented several ways, which `texts` holds.
  std::vector<const RowDesc*> rows;
  std::vector<std::unique_ptr<const RowDesc>> texts;
  // Folded literal alternatives and their word boundaries, in fold mark order.
  std::vector<std::vector<FoldedWord>> foldedSets;

  // `entry`, which may be none, then `action`.
  std::uint32_t extended(std::uint32_t entry, std::uint16_t action) {
    const auto [it, fresh] = _entries.try_emplace(std::uint64_t{entry} << 16 | action,
                                                  static_cast<std::uint32_t>(entries.size()));
    if (fresh) {
      if (entries.size() >= kEntryLimit)
        throw Refused("the grammar has too many records");
      entries.push_back({entry, action});
    }
    return it->second;
  }
  // `inner`'s actions, then `outer`'s: the record of a lone child taken over
  // by the record around it. Either may be none.
  std::uint32_t fuse(std::uint32_t inner, std::uint32_t outer) {
    _outer.clear();
    for (; outer != kNone; outer = entries[outer].before)
      _outer.push_back(entries[outer].action);
    for (std::size_t i = _outer.size(); i-- > 0;) inner = extended(inner, _outer[i]);
    return inner;
  }

 private:
  // `before << 16 | action`, the entry.
  std::unordered_map<std::uint64_t, std::uint32_t> _entries;
  std::vector<std::uint16_t> _outer;
};

}  // namespace cursiv::engine::gen
