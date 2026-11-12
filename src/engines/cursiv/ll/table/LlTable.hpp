// Everything a generated module hands its engine: pointers into the generated
// arrays and their counts. The arrays are `inline constexpr`, so a matcher
// templated on the table folds every bound it reads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/ll/table/LlAtnView.hpp"
#include "cursiv/ll/table/LlDecision.hpp"
#include "cursiv/ll/table/LlLexAccept.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/CondReach.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

struct LlContext;

// One row of terminals, at the width the grammar needs. A frozen set: the
// construction's own grows, this one is a fixed run of words.
struct LlTermRow {
  const std::uint64_t* w = nullptr;
  std::uint32_t words = 0;

  constexpr bool test(std::uint16_t t) const {
    const std::uint32_t k = t >> 6;
    return k < words && ((w[k] >> (t & 63)) & 1) != 0;
  }
};

using LlConditionRead = bool (*)(const char*, TextOffset, unsigned,
                                 const Token&, TextOffset, bool, TextOffset*);
using LlConditionNamed = unsigned (*)(const char*, TextOffset, unsigned,
                                      const Token&, TextOffset);

struct LlTable {
  struct Node {
    std::uint16_t row = kNoRow;
    // The alphabet the position reads under; an operator a climb switches on
    // reads the climb's.
    std::uint16_t mode = kNoRow;
    // The terminal a position reads, `kNoRow` where it reads none.
    std::uint16_t term = kNoRow;
    bool head = false;
    // How many records that name their rule stand open around this position.
    std::uint8_t covers = 0;
  };
  struct Decision {
    // A climb's choice of operator, which a plan answers each time it is
    // asked: the climb asks it once per level it unwinds over one token.
    bool again = false;
    LlPredict how = LlPredict::single;
    std::uint16_t mode = kNoRow;
    std::uint32_t state = 0;
  };

  // The grammar, as the tree asks about it. `names` holds the bytes the
  // terminals and the relays name, each range by offset and length.
  const char* names = nullptr;
  const TermInfo* terms = nullptr;
  std::uint16_t nterms = 0;
  std::uint16_t nrules = 0;
  const Node* nodes = nullptr;
  const Decision* decisions = nullptr;
  std::uint16_t ndecisions = 0;
  LlConditionRead condition = nullptr;
  LlConditionNamed conditionNamed = nullptr;
  std::uint16_t ngapConds = 0;
  CondReach condReach{};
  // What a repair reads: the tokens a choice admits. A parse that finds none
  // skips text until it does.
  const std::uint64_t* decFirst = nullptr;
  // What a caret walks: the state the graph stands in once each position is
  // read, the position each token edge belongs to, and where a document that
  // has read nothing starts.
  const std::uint32_t* nodeAfter = nullptr;
  // The alphabet the gap after a position is crossed under, where every
  // reading that may follow agrees on it; null where the grammar has no
  // regions.
  const std::uint16_t* nodeAfterGap = nullptr;
  // The operator step a position opens, in a climb with a step that groups
  // nothing: the climb plus one in the high half, then the step's number and
  // whether it groups nothing; null where no climb has such a step.
  const std::uint32_t* nodeStep = nullptr;
  std::uint16_t nafter = 0;
  const std::uint16_t* edgeNode = nullptr;
  const std::uint16_t* returnNode = nullptr;
  std::uint32_t atnStart = 0;

  // The lexer. `setDecode` names, per set, the lead bytes at which the word
  // class's scalar decides, `kNoSet` for a plain class; null without word or
  // scalar ranges.
  const ByteSet* sets = nullptr;
  const std::uint16_t* setDecode = nullptr;
  ByteSet wordBytes{};
  const unicode::Range* wordRanges = nullptr;
  std::uint16_t nwordRanges = 0;
  // Per set decoding another class, its scalars in `scalarRanges`: `at << 16
  // | count`, zero for the word class's; null where no set does.
  const std::uint32_t* setScalars = nullptr;
  const unicode::Range* scalarRanges = nullptr;
  // Whether a word terminal `t` refuses behind its head is spelled by the
  // token at `p`; null where no terminal refuses one.
  bool (*head_refuses)(SymbolId t, const char* p, TextOffset rest) = nullptr;
  // How far the words of mode `m`'s refusing terminals spelled whole at `p`
  // are read, as a walk through them reads them.
  TextOffset (*head_reach)(unsigned m, const char* p,
                           TextOffset rest) = nullptr;
  // The same for the words terminal `t` refuses, which a walk need not read:
  // its token may end before them.
  TextOffset (*termReach)(SymbolId t, const char* p, TextOffset rest) = nullptr;
  // Whether a word terminal `t` refuses where its token ends is spelled at
  // `p`; null where no terminal refuses one there.
  bool (*tailRefuses)(SymbolId t, const char* p, TextOffset rest) = nullptr;
  const std::uint8_t* cls = nullptr;
  std::uint8_t ncls = 0;
  std::uint16_t nmodes = 0;
  std::uint16_t errMode = 0;
  std::uint16_t tailMode = 0;
  // Per mode, the mode a fault is lexed with: the terminals of the mode's
  // configuration. Null where the grammar has one, which reads `errMode`.
  const std::uint16_t* errModeOf = nullptr;
  // What a lookahead crosses trivia with, per choice: the error mode read
  // under the choice's skip configuration. Null where every choice reads the
  // grammar's.
  const std::uint16_t* lookMode = nullptr;
  const std::uint16_t* modeStart = nullptr;
  const std::uint8_t* modeMulti = nullptr;
  const std::uint8_t* modeBacktrack = nullptr;
  constexpr bool has_backtrack() const { return modeBacktrack != nullptr; }
  // Per mode, the mode that lexes what it takes for the length of its token
  // alone; null where no mode has one.
  const std::uint16_t* modeLonger = nullptr;
  constexpr bool has_longer() const { return modeLonger != nullptr; }
  constexpr std::uint16_t longer_mode(std::uint16_t mode) const {
    return modeLonger[mode];
  }
  // Trivia is elected independently of visible tokens during prediction.
  // Another skip configuration may read the same spelling as a token.
  const std::uint16_t* modeTrivia = nullptr;
  const std::uint16_t* modeJoinedTrivia = nullptr;
  const std::uint64_t* modeTerms = nullptr;
  const std::uint64_t* modeSub = nullptr;
  std::uint32_t subWords = 0;
  // Per mode, four words of the bytes a run of blanks is made of: such a run
  // is the one unrecorded trivia token the walk would read there. Null where
  // no mode crosses any blind.
  const std::uint64_t* skipSet = nullptr;
  // The bytes such a run's last byte may read on with: a reader steps back onto
  // that byte and reads on from it. Null where no mode has any.
  const std::uint64_t* skipStop = nullptr;
  // Per mode, the one-byte literal each first byte reads whole,
  // `kInvalidSymbol` where the walk reads it: rows of 256, and per mode where
  // its row starts. Null where no mode reads one.
  const std::uint16_t* byteTerms = nullptr;
  const std::uint32_t* modeBytes = nullptr;
  // The walk of every mode, generated: the token a mode reads at a position,
  // as `lex_walk` reads it over these rows.
  Scan (*walk)(const char*, TextOffset, std::uint16_t, TextOffset) = nullptr;
  const std::uint16_t* lexNext = nullptr;
  const std::uint32_t* typoSuccBase = nullptr;
  const std::uint16_t* typoSucc = nullptr;
  // What a reader of a refused token needs off the automaton: the states that
  // loop in a body, the terminal each is on the way to, and the bytes a mode
  // opens such a body with.
  const std::uint8_t* lexBodies = nullptr;
  const std::uint16_t* lexBodyTerms = nullptr;
  const ByteSet* lexOpeners = nullptr;
  const std::uint16_t* dfaAccept = nullptr;
  const std::uint16_t* dfaBound = nullptr;
  const std::uint32_t* lexChoiceAt = nullptr;
  const LlLexAccept* lexChoices = nullptr;
  const std::uint8_t* lexChecks = nullptr;
  const std::uint16_t* dfaRelay = nullptr;
  const LexRelay* relays = nullptr;
  std::uint16_t nrelays = 0;
  TextOffset (*relay_run)(std::uint16_t, const char*, TextOffset,
                          TextOffset) = nullptr;
  // The terminals a state accepts and those it reserves, pooled: the row of
  // state `s` starts at word `lexAccAt[s]` of `lexAcc`. No state reserves
  // where `lexRes` is null.
  const std::uint64_t* lexAcc = nullptr;
  const std::uint32_t* lexAccAt = nullptr;
  const std::uint64_t* lexRes = nullptr;
  const std::uint32_t* lexResAt = nullptr;
  // What a walk can still accept from a state on, the state included.
  const std::uint64_t* lexLive = nullptr;
  const std::uint32_t* lexLiveAt = nullptr;
  // What a keyword read behind conditions takes in a state, if it stands.
  const std::uint64_t* lexRelayRes = nullptr;
  const std::uint32_t* lexRelayResAt = nullptr;
  // The sets of a word-bounded acceptance, whose terminals reserve; no other
  // reading of a terminal reserves anything.
  std::span<const std::uint16_t> wordBounds{};
  // Some state reserves a spelling from a terminal that reads it as a pattern.
  bool reserves = false;
  static constexpr bool standsWithoutWordClass = false;
  std::uint32_t termWords = 0;

  // The graph a prediction walks.
  LlAtnView atn{};
  // What a caller reads once its call is done, per return state: the terminals
  // at word `callerReadAt[s] >> 1` of `callerReads`, the low bit set where the
  // rule may end reading none. Null where nothing asks.
  const std::uint64_t* callerReads = nullptr;
  const std::uint32_t* callerReadAt = nullptr;
  // Per choice, the terminals its alphabet admits for a caller only that read
  // over another, at word `decFlagAt[d]` of `decFlag`: the descent takes one
  // only where a caller does. Null where no choice has any.
  const std::uint64_t* decFlag = nullptr;
  const std::uint32_t* decFlagAt = nullptr;
  std::uint16_t (*staged)(const LlContext&, std::uint16_t) = nullptr;
  // What an episode reads besides the graph: per rule, whether callers two ways
  // out of one choice share its body; per choice, whether it is a repetition
  // that ranks its arms over lexical choices. Null where none is.
  const std::uint8_t* sharedRules = nullptr;
  const std::uint8_t* repeatScopes = nullptr;

  // Per section configuration: the mode its text is read in, whole, its
  // grammar's terminals and its closing token, and that token, which a repair
  // there never invents or drops; per position, the section it stands in, as
  // `LlContext::section` names it. Null where the grammar embeds none.
  const std::uint16_t* sectionErr = nullptr;
  const std::uint16_t* sectionEnd = nullptr;
  const std::uint8_t* nodeSection = nullptr;
  std::uint8_t nsections = 0;

  StampLayout stamp{};

  // Whether the wider alphabet admits all the narrower one does. The bound
  // check states that `wide` names a row, which no reader can tell from the
  // pointer.
  constexpr bool mode_covers(std::uint16_t wide, std::uint16_t narrow) const {
    if (wide >= nmodes) return false;
    return ((modeSub[std::size_t{wide} * subWords + (narrow >> 6)] >>
             (narrow & 63)) &
            1) != 0;
  }
  constexpr bool mode_admits(std::uint16_t m, std::uint16_t t) const {
    return ((modeTerms[std::size_t{m} * termWords + (t >> 6)] >> (t & 63)) &
            1) != 0;
  }
  constexpr std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    return lexNext[std::size_t{s} * ncls + cls[b]];
  }
  constexpr LlTermRow dfa_acc_set(std::uint16_t s) const {
    return LlTermRow{lexAcc + lexAccAt[s], termWords};
  }
  constexpr LlTermRow dfa_live_set(std::uint16_t s) const {
    return LlTermRow{lexLive + lexLiveAt[s], termWords};
  }
  constexpr LlTermRow dfa_res_set(std::uint16_t s) const {
    return lexRes == nullptr
               ? LlTermRow{}
               : LlTermRow{lexRes + lexResAt[s], termWords};
  }
  constexpr LlTermRow dfa_relay_res_set(std::uint16_t s) const {
    return lexRelayRes == nullptr
               ? LlTermRow{}
               : LlTermRow{lexRelayRes + lexRelayResAt[s], termWords};
  }
  template <class F>
  constexpr bool each_accept(std::uint16_t s, F&& f) const {
    if (lexChoices == nullptr) return false;
    const auto first = lexChoiceAt[std::size_t{s} * 2];
    const auto end = first + lexChoiceAt[std::size_t{s} * 2 + 1];
    for (auto r = first; r < end; ++r)
      if (f(lexChoices[r].term, lexChoices[r].bound,
            unsigned{lexChoices[r].priority}))
        return true;
    return false;
  }
  constexpr bool lex_bounded(std::uint16_t s) const {
    return dfaBound[s] != kNoSet || (lexChecks != nullptr && lexChecks[s] != 0);
  }
  constexpr std::uint16_t look_mode(const Decision& d) const {
    return lookMode != nullptr
               ? lookMode[static_cast<std::size_t>(&d - decisions)]
               : errMode;
  }
  constexpr const std::uint64_t* dec_first(std::uint16_t d) const {
    return decFirst + std::size_t{d} * termWords;
  }
  constexpr const std::uint64_t* dec_flag(std::uint16_t d) const {
    return decFlag + decFlagAt[d];
  }
  // Bounded, and it must be: a byte no terminal matches leaves the lookahead
  // naming no symbol at all, and that name is wider than any row.
  constexpr bool holds(const std::uint64_t* row, std::uint16_t t) const {
    return t < nterms && ((row[t >> 6] >> (t & 63)) & 1) != 0;
  }
  constexpr std::string_view term_name(std::uint16_t t) const {
    return {names + terms[t].nameOff, terms[t].nameLen};
  }
  constexpr std::string_view term_text(std::uint16_t t) const {
    return {names + terms[t].textOff, terms[t].textLen};
  }
};

template <const auto& T>
inline bool ll_condition_text(std::string_view text, std::uint16_t condition,
                              const Token& token, TextOffset gapFrom,
                              bool atEnd = false, TextOffset* seen = nullptr) {
  if constexpr (T.condition == nullptr)
    return false;
  else
    return T.condition(text.data(), static_cast<TextOffset>(text.size()),
                       condition, token, gapFrom, atEnd, seen);
}

// What a condition answers of a token of `token.sym` whose bytes a caret does
// not have: 0 false, 1 unknown, 2 true. A head leaf a terminal's body decides
// answers; the gap before the token is read.
template <const auto& T>
inline unsigned ll_condition_named(std::string_view text,
                                   std::uint16_t condition, const Token& token,
                                   TextOffset gapFrom) {
  if constexpr (T.conditionNamed == nullptr)
    return 0;
  else
    return T.conditionNamed(text.data(), static_cast<TextOffset>(text.size()),
                            condition, token, gapFrom);
}

}  // namespace cursiv::engine
