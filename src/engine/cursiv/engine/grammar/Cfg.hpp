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
  bool capture = false;
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
  // keeps its own; may be empty where no symbol's does.
  std::vector<std::uint32_t> takes;
  // The infix node it copies under a test of the gap turned the other way,
  // reduced where a tighter level read past that test; none for an
  // alternative of its nonterminal.
  std::uint32_t copyOf = kNone;
  // Its symbols from `at` on, `count` of them, gone with their scopes and
  // the entries their records take.
  void erase(std::size_t at, std::size_t count) {
    const auto cut = [&](auto& per) {
      if (per.empty()) return;
      const auto from = per.begin() + static_cast<std::ptrdiff_t>(at);
      per.erase(from, from + static_cast<std::ptrdiff_t>(count));
    };
    cut(rhs);
    cut(scopes);
    cut(takes);
  }
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
// The most actions a grammar publishes: a completion word holds an action's
// number in 14 bits.
inline constexpr std::size_t kActionLimit = std::size_t{1} << 14;

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
    // Per token that operators of tighter levels read past a test of the
    // gap, a copy of the node reduced on it where that test fails; ascending.
    std::vector<std::pair<std::uint16_t, std::uint32_t>> past;
  };
  // The tightest first.
  std::vector<Level> levels;
  // A node of level `l` against the binary or postfix operators of levels
  // `met` that one token begins: whether it is reduced before one, and
  // whether one may come. A postfix node ends with its operator, so no
  // operator takes its operand: both stand.
  [[nodiscard]] std::pair<bool, bool> order(std::uint32_t l, const auto& met) const {
    const Association group = levels[l].group;
    if (group == Association::Postfix) return {true, true};
    bool before = false, coming = false;
    for (const std::uint32_t m : met) {
      before = before || l < m || (l == m && group == Association::Left);
      coming = coming || m < l || (l == m && group == Association::Right);
    }
    return {before, coming};
  }
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
  // brackets that open with line ends muted, those that open a block where
  // they count, those that close; the terminal that ends a line, it and the
  // copies operators read of it, and the trivia whose line ends end one too.
  std::uint16_t indent = 0;
  std::uint16_t dedent = 0;
  std::vector<std::uint16_t> opens, blocks, closes;
  std::uint16_t newline = 0;
  std::vector<std::uint16_t> lines, newlineIn;
  // The nonterminals that may read nothing: no terminal, every symbol one
  // that may.
  [[nodiscard]] std::vector<bool> nullable() const {
    std::vector<bool> blank(nonterminals, false);
    std::vector<std::uint32_t> waits(productions.size(), kNone), work;
    std::vector<std::vector<std::uint32_t>> within(nonterminals);
    for (std::uint32_t p = 0; p < productions.size(); ++p) {
      const Production& q = productions[p];
      if (std::ranges::any_of(q.rhs, [](Symbol s) { return is_terminal(s); })) continue;
      waits[p] = static_cast<std::uint32_t>(q.rhs.size());
      for (const Symbol s : q.rhs) within[index_of(s)].push_back(p);
      if (q.rhs.empty() && !blank[q.lhs]) {
        blank[q.lhs] = true;
        work.push_back(q.lhs);
      }
    }
    while (!work.empty()) {
      const std::uint32_t nt = work.back();
      work.pop_back();
      for (const std::uint32_t p : within[nt]) {
        const std::uint32_t lhs = productions[p].lhs;
        if (--waits[p] != 0 || blank[lhs]) continue;
        blank[lhs] = true;
        work.push_back(lhs);
      }
    }
    return blank;
  }
  Nesting nesting(std::uint16_t terminal) const {
    if (terminal == indent && terminal != 0) return Nesting::indent;
    if (terminal == dedent && terminal != 0) return Nesting::dedent;
    if (std::ranges::binary_search(opens, terminal)) return Nesting::open;
    if (std::ranges::binary_search(blocks, terminal)) return Nesting::block;
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
    bool conditional;
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
  struct Opening { std::uint32_t mark, depth; bool writes; };
  std::vector<std::vector<Opening>> openings;
  // The children a reduction writes when empty prefixes are held at the opening.
  struct RecordInputs {
    std::uint32_t production;
    std::vector<Symbol> rhs;
    std::vector<std::uint32_t> takes;
  };
  std::vector<RecordInputs> recordInputs;
  void swapRecordInputs() {
    for (auto& input : recordInputs) {
      auto& p = productions[input.production];
      p.rhs.swap(input.rhs);
      p.takes.swap(input.takes);
    }
  }
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
