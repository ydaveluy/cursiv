// What the emitter writes of a language: the lexer's tables as the runtime
// reads them, and the facts its code is made of.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/engine/grammar/GrammarImage.hpp"
#include "cursiv/engine/parser/Bits.hpp"
#include "cursiv/engine/lexer/ByteSet.hpp"
#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/lexer/LexAutomaton.hpp"
#include "cursiv/engine/parser/Lr1.hpp"
#include "cursiv/engine/parser/Tables.hpp"

namespace cursiv::engine::gen {

// Per terminal `u`: in `reads`, the terminals that read as far as `u` or
// further on some text `u` reads; in `takes`, those whose token may stand
// where `u` reads: they read on past where `u` ends, but by a byte that goes
// on a word where `u` is a keyword, or end there and rank before it.
struct LexRivals {
  std::vector<RivalRow> reads;
  std::vector<RivalRow> takes;
};
LexRivals lex_rivals(const GrammarImage& image, const Cfg& cfg, const LexDfa& dfa);

// What a mode knows of a token from its first byte: `kFirstLiterals | index`
// where it is the longest of a list of literals; `kFirstWord | index` where
// it is that byte and the run of a `WordPath`; 0 where the lexer's automaton
// reads it.
inline constexpr std::uint32_t kFirstLiterals = 1u << 30;
inline constexpr std::uint32_t kFirstWord = 2u << 30;
inline constexpr std::uint32_t kFirstIndex = kFirstLiterals - 1;
inline constexpr std::uint16_t kNoMode = 0xFFFF;

// A text a `WordPath` reads apart: its bytes, letters in either case where
// `folded`, and its token, `kNoToken` where the automaton reads it.
struct Spelling {
  std::string text;
  bool folded = false;
  std::uint16_t terminal = kNoToken;
  auto operator<=>(const Spelling&) const = default;
};

// A token read by `run`, with `wide` leaving non-ASCII continuations to the
// automaton: `terminal`, or what a text of `spelled` reads, where no word
// goes on past it if the run can stop before a byte of a word.
struct WordPath {
  std::uint16_t terminal = 0;
  std::uint32_t run = 0;
  bool wide = false;
  std::vector<Spelling> spelled;
};

struct PackedAction {
  // `reread`: the text of the token in hand read again by the state in
  // another mode, where the stack takes none of the token's other actions.
  enum class Kind : std::uint8_t { shift, reduce, accept, reread, retryTie };
  std::uint16_t terminal = 0;
  Kind kind = Kind::shift;
  // A shift's state, a reduce's production, a reread's mode.
  std::uint32_t value = 0;
  // The leaf's entry where the action shifts; the state a reread is of.
  std::uint32_t entry = kNone;
  // Where it shifts, the unit productions its step takes: a list of
  // `Packed::hops`.
  std::uint32_t hops = 0;
  // Where it shifts, what the text before the token must read as: anything
  // the lexer passed, nothing, or the trivia of mode `gap` alone, which also
  // reads the token.
  std::uint16_t gap = kAnyGap;

  // Other doubtful terminals the fallback lexer removed along with this one.
  std::uint32_t rivals = 0;

  // Actions share code when they treat the stack, tree, gap and indentation alike.
  using Key = std::tuple<Kind, std::uint32_t, std::uint32_t, std::uint16_t, Nesting, std::uint32_t>;
  Key key(const Cfg& cfg) const {
    return {kind, value, entry, gap, cfg.nesting(terminal), rivals};
  }
};

struct PackedState {
  // Its row among the modes.
  std::uint16_t mode = 0;
  // The production it reduces without reading a token, if any.
  std::uint32_t reduce = kNone;
  // Ascending terminals.
  std::vector<PackedAction> actions;
  // The terminals a repair may have to insert in it.
  Bits close;
  // Those a repair tries first, in order, where that is not their numbers'.
  std::vector<std::uint16_t> inserts;
  // The terminals its tests name, and those the parse reads after them: its
  // lexer reads them too.
  std::vector<std::uint16_t> named;
  std::vector<std::uint16_t> follow;
  // Whether a token in hand that another state's lexer read is read again
  // here: this lexer may cut the text otherwise. The end of input is not.
  [[nodiscard]] bool rereads() const { return !named.empty() || !follow.empty(); }
  // The terminals that may read a trivia's text and that it neither reads
  // nor acts on: a token of one in hand, it reads that text again.
  std::vector<std::uint16_t> passes;
  // The terminals it reads only for a reduction whose tests may refuse the
  // token in hand, and the mode it reads that text again in where they do:
  // its own without them. `kNoMode` where it has none.
  std::vector<std::uint16_t> tested;
  std::uint16_t again = kNoMode;
  // The lexical modes that can dispatch a token in this state.
  template <class Each>
  void eachMode(Each each) const {
    each(mode);
    if (again != kNoMode) each(again);
    for (const auto& a : actions)
      if (a.kind == PackedAction::Kind::reread) each(static_cast<std::uint16_t>(a.value));
  }
  // As `LrState::doubted`.
  std::vector<std::uint16_t> doubted;
  // The trivia scopes its tokens are read under, a bit a scope.
  std::uint64_t scopes = 1;
  // What a symbol read into it over the left operand of a left recursion
  // begins, as `Tables::turnOf` and `Tables::turnVia` tell it.
  std::uint32_t turn = kNoProduction;
  std::uint32_t via = kNoProduction;
};

// Where a nonterminal reduced over state `from` leads, and what the call site
// adds to the record just closed.
struct PackedGoto {
  std::uint32_t from = 0;
  std::uint32_t target = 0;
  std::uint32_t entry = kNone;
  // The unit productions its step takes: a list of `Packed::hops`.
  std::uint32_t hops = 0;
};

struct Packed {
  // The lexer's tables, as `Tables` names them.
  std::array<std::uint8_t, 256> byteClass{};
  std::vector<std::uint16_t> lexNext;
  std::vector<std::uint32_t> nextBlocks, nextOf;
  std::uint16_t transition(std::uint32_t state, unsigned byte) const {
    return LexBlocks{lexNext.data(), nextBlocks.data()}.at(nextOf[state], byteClass[byte]);
  }
  std::vector<std::uint16_t> lexAccept;
  std::vector<std::uint8_t> lexRun;
  std::vector<std::uint8_t> runSets;
  std::vector<std::uint16_t> lexWin;
  std::vector<std::uint32_t> winBlocks, winOf, onwardOf;
  std::vector<LexResolution> resolutions;
  std::array<std::uint8_t, 256> wordBytes{};
  std::vector<unicode::Range> wordRanges;
  bool wordInvalid = false;
  std::uint16_t lexClassCount = 0;
  std::vector<std::uint8_t> lexOnward;
  std::uint32_t onwardRow = 0;
  LexPaths paths;
  // Sets of terminals, each written once, a row of a word per 64 terminals.
  // Per lexer state, the terminals that end in it or past it; per state, those
  // it has an action for, those a repair may have to insert there, and those
  // whose insertion there may be required.
  std::vector<std::uint64_t> terminalSets;
  std::vector<std::uint16_t> lexReachOf;
  std::vector<std::uint16_t> lexTieEnds;
  std::vector<std::uint16_t> followOf;
  std::vector<std::uint16_t> closeOf;
  std::vector<std::uint16_t> mayCloseOf;
  std::vector<bool> lexTrivia;  // A trivia can end at or beyond this DFA state.
  // What reads the terminals the automaton reads the first bytes of only,
  // and per state of its automaton, one more than its set of `runSets`.
  LexMatcher matcher;
  std::vector<std::uint8_t> matchRun;
  // The matcher's transitions in shared blocks, as `lexNext`.
  std::vector<std::uint16_t> matchNext;
  std::vector<std::uint32_t> matchBlocks, matchOf;
  // The conditions left to be read once a token ends, as the automaton has
  // them.
  std::vector<LexMonitor> monitors;
  std::vector<std::vector<std::array<std::uint32_t, 3>>> waits;
  std::vector<Trivia> terminalTrivia;
  std::vector<std::uint8_t> terminalRank;
  std::vector<std::uint32_t> terminalEntry;
  std::vector<std::uint16_t> terminalElement;

  // The automaton, unit steps taken out: states numbered from the start, and
  // per nonterminal its targets.
  std::vector<PackedState> states;
  std::vector<std::vector<PackedGoto>> gotos;
  // Per condition of the grammar, the state what it reads is read from.
  std::vector<std::uint32_t> conditions;
  // Each unit extends its outer chain with one production; chain 0 is empty.
  struct Unit {
    std::uint32_t outer, production;
  };
  std::vector<Unit> hops{{0, 0}};
  // The runtime unit table and the offsets of the chains used by a transition.
  std::vector<std::uint32_t> unitWords(std::vector<std::uint32_t>& number) const;
  // Per production, how it stands among those that read one text: `rank <<
  // 2`, bit 1 where its nonterminal is left recursive, bit 0 where it is a
  // turn of that recursion. Bases and turns are ranked apart, as a walk from
  // the recursion's entry reaches them.
  std::vector<std::uint32_t> order;

  // The modes: `lexFirst[mode * 256 + byte]`, and the width of a mode's row
  // of `lexWin`.
  std::uint16_t modeCount = 0;
  // Per mode, whether it passes every terminal the grammar drops and reads
  // none of them as a token: the bytes of `skipped` are then dropped unread.
  std::vector<bool> modeSkips;
  // The most tokens the shortest reading of a production takes.
  std::uint64_t closeMost = 0;
  // Per nonterminal, whether it can read nothing.
  std::vector<bool> nullable;
  // Per mode, the mode a fault of its states is read in: the terminals
  // shifted where they stand, and what any scope passes.
  std::vector<std::uint16_t> modeFault;
  // Per mode, the terminal that closes the section its states read in;
  // `kNoToken` where they read in none, or in several.
  std::vector<std::uint16_t> modeClose;
  // Each mode's alphabet, and the terminals read only for its tests.
  std::vector<Bits> modeTerminals, modeNamed;
  // Terminals that can read the same token in a mode, the lexer's choice first.
  struct Tie {
    std::vector<std::uint16_t> terminals;
    // Its row of tieSets, or kNone to check the terminals in their own modes.
    std::uint32_t row = kNone;
  };
  std::vector<Tie> ties;
  std::vector<Bits> tieSets;
  std::vector<std::vector<std::uint16_t>> retryTerminals{{}};
  std::vector<std::uint16_t> solo;  // By terminal, its mode for checking a delayed tie.
  std::vector<bool> mixed;  // Terminals with both literal and pattern branches.
  std::vector<std::vector<std::uint32_t>> modeTies;
  std::uint32_t winRow = 0;
  std::vector<std::uint32_t> lexFirst;
  std::vector<WordPath> wordPaths;
  // Lists of literals, the longest first.
  std::vector<std::vector<std::uint16_t>> literalPaths;
  // The sets of `runSets`, as sets.
  std::vector<ByteSet> runBytes;
  // The bytes dropped before a token without being read as one, and those
  // that may go on what was dropped: the lexer's walk reads from the first
  // byte dropped where one of them follows.
  ByteSet skipped;
  ByteSet resumed;

  // The entries a record can carry, in the order the tape numbers them, and
  // each one's number: what the drive stamps.
  std::vector<std::uint32_t> elements;
  std::size_t deferred = 0;
  std::unordered_map<std::uint32_t, std::uint16_t> elementOf;
  [[nodiscard]] std::uint32_t element(std::uint32_t entry) const {
    return entry == kNone ? std::uint32_t{kNoElement} : elementOf.at(entry);
  }

  // Per production, what it can find written when it closes its record:
  // `kRecordEmpty`, `kRecordLone`, `kRecordSeveral`.
  std::vector<std::uint8_t> shapes;
  // `(own, site)`: the entry of a record closed with `own` once its call
  // site adds `site`.
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> composed;
  // `(outer, inner)`: the entry of a lone child's record, `inner`, taken over
  // by the record closed with `outer` around it.
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> fused;

  // The entry a production closing `own` leaves at a call site that adds
  // `site`; either may be none.
  [[nodiscard]] std::uint32_t closed(std::uint32_t own, std::uint32_t site) const {
    if (own == kNone) return site;
    if (site == kNone) return own;
    return composed.at({own, site});
  }

  // What the generator says of the grammar without refusing it.
  std::vector<std::string> warnings;
};

// Interned prefix proofs and their decisions by cell, frame depth and state,
// with the walks they are made of, each read once.
struct PrefixPredictions {
  static constexpr std::uint8_t kUnread = 0xFF;
  // From a goto site with a token held: shifted after `steps`, or gone
  // `missing` frames under the site's state to go on `lhs`, or stopped.
  struct Climb {
    std::uint32_t lhs = 0;
    std::uint16_t missing = 0;
    std::uint8_t steps = kUnread;
    bool shifted = false;
  };
  // A walk of a reader's token that goes `depth` frames under the cell to
  // go on `lhs`, after `steps`.
  struct Under {
    std::uint16_t terminal;
    std::uint8_t steps;
    std::uint32_t depth, lhs;
    auto operator<=>(const Under&) const = default;
  };
  // Rows ordered by their bytes read as text: the vectors' own order, whose
  // comparison some GCC releases flag at -O3.
  struct BytesLess {
    bool operator()(const std::vector<std::uint8_t>& a,
                    const std::vector<std::uint8_t>& b) const noexcept {
      const auto text = [](const std::vector<std::uint8_t>& v) {
        return std::string_view(reinterpret_cast<const char*>(v.data()), v.size());
      };
      return text(a) < text(b);
    }
  };
  // By next token, whether a reader shifts it: 0 it reads it not, 1 it
  // reads it without a known shift, 2 it shifts it. Its walks that go under
  // it apart, by nonterminal, and the depth the first in reading order goes to.
  struct Row {
    const std::vector<std::uint8_t>* shifts = nullptr;
    const std::vector<Under>* under = nullptr;
    std::uint32_t deeper = 0;
  };
  // A goto site's climbs. Where its target reads a token, `columns` places a
  // token's climb by the target's one action on it; where the target reduces
  // at once, the token does, or one place holds all where they climb alike.
  struct Site {
    std::uint32_t target;
    std::vector<Climb> climbs;
    const std::uint32_t* columns = nullptr;
  };
  // The walks every cell's prediction shares, dropped once the cells are
  // predicted.
  struct Cache {
    std::vector<std::vector<std::uint32_t>> predecessors;
    std::unordered_map<std::uint64_t, Site> sites;
    std::vector<Bits> tied;
    std::vector<std::uint32_t> readerOf;
    std::vector<std::uint32_t> visited;
    std::uint32_t generation = 0;
    std::vector<std::vector<std::uint32_t>> actions;
    // By reader's stack, the frames under it last; the parts of rows, each once.
    std::map<std::vector<std::uint32_t>, Row> rows;
    std::set<std::vector<std::uint8_t>, BytesLess> shifts;
    std::set<std::vector<Under>> under;
  };
  Cache cache;
  std::map<std::vector<std::uint8_t>, std::uint32_t, BytesLess> shifts;
  std::map<std::vector<std::array<std::uint32_t, 3>>, std::uint32_t> plans;
  std::map<std::vector<std::uint32_t>, std::uint32_t> stacks;
  struct Path {
    std::uint32_t stack, plan;
    std::vector<std::array<std::uint16_t, 2>> tokens;
    auto operator<=>(const Path&) const = default;
  };
  std::map<Path, std::uint32_t> paths;
  // A cell's plan by the state `depth` frames down: `fallback`, but where
  // `contexts` holds another; kNone for none.
  struct Decision {
    std::uint32_t fallback = kNone;
    std::map<std::uint32_t, std::uint32_t> contexts;
  };
  std::map<std::pair<std::uint32_t, std::uint32_t>, Decision> cells;
};

// The plan of the cell whose arms are `actions`; with `tied`, each arm takes
// the token in hand as its own action's terminal.
void prefix_predictions(PrefixPredictions& out, const Cfg& cfg, const Packed& packed,
    std::uint32_t state, std::uint32_t cell, std::span<const PackedAction> actions, bool tied);

// `cfg` gains the entries the transitions compose; `takes` as `lex_rivals`
// gives it. Throws `Refused` for what the engine does not read.
Packed pack(const GrammarImage& image, Cfg& cfg, LexDfa dfa, const std::vector<RivalRow>& takes,
            const LrAutomaton& automaton);

}  // namespace cursiv::engine::gen
