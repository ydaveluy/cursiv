// What the construction adds to a lowered program: the graph a prediction
// walks, what every position may begin and be followed by, and how each choice
// is decided.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/ll/gen/LlLowCfg.hpp"
#include "cursiv/ll/table/LlAtn.hpp"
#include "cursiv/ll/table/LlDfa.hpp"
#include "cursiv/ll/table/LlFuse.hpp"
#include "cursiv/ll/table/LlLexAccept.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"
#include "cursiv/shared/gen/GrammarFault.hpp"
#include "cursiv/shared/gen/HeadMatcher.hpp"
#include "cursiv/shared/gen/LexRelayGen.hpp"
#include "cursiv/shared/gen/ShapeOverflow.hpp"
#include "cursiv/shared/gen/Vec.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/StampLayout.hpp"

namespace cursiv::engine {

// Whether a site reads its run of joins from `kFuse` instead of emitting it
// inline: more than one join, or any join of an owning group (`owning`).
constexpr bool ll_fuse_tabled(std::uint32_t joins, bool owning) {
  return joins > (owning ? 0u : 1u);
}

struct LlBuiltProgram : ShapeOverflow {
  LlLowCfg cfg{};
  ChainPool writes{};

  // The graph, one state per position of a body and one edge per way out.
  Vec<LlAtnState> states{};
  std::uint32_t nstates = 0;
  Vec<LlAtnEdge> edges{};
  std::uint32_t nedges = 0;
  std::vector<std::uint32_t> ruleStart{};
  std::vector<std::uint32_t> callerBase{};
  std::vector<std::uint32_t> callerAt{};
  std::vector<std::uint32_t> returnClass{};

  // Per position, then per rule from `cfg.nnodes` on: what may begin here,
  // whether it can read nothing, and the conditions no cut decides that stand
  // at its head.
  std::vector<DynSet> first{};
  std::vector<std::uint8_t> nullable{};
  std::vector<DynSet> lead{};
  // What may follow a position, and a rule.
  std::vector<DynSet> after{};
  std::vector<DynSet> ruleFollow{};
  // What a position may begin with, plus what follows it when it can read
  // nothing.
  DynSet lookahead(std::uint16_t id, const DynSet& follow) const {
    DynSet s = first[id];
    if (nullable[id]) s |= follow;
    return s;
  }
  // The rules the entry reaches, a climb's operators and operand included.
  std::vector<bool> liveRules{};

  // Per condition, the tokens it refuses where the current token alone decides
  // it (a commit's exit); empty for any other.
  std::vector<DynSet> cut{};
  // One row per arm of every decision, which is what a switch reads.
  std::vector<DynSet> laSet{};
  // The automata the exploration closed, in one pool.
  std::vector<LlDfaState> dfa{};
  std::vector<LlDfaEdge> dfaEdge{};
  // Where the document is entered: the entry rule has a caller like every
  // other, or a closure could not read past its own end.
  std::uint32_t atnEntry = 0;

  // The lexer: the byte classes every edge is written in, one automaton for
  // all the modes at once, and where each mode starts in it.
  std::uint8_t cls[256]{};
  std::uint8_t ncls = 0;
  std::uint16_t nmodes = 0;
  // The mode no position owns: it lexes every terminal, as a fault is lexed
  // where the grammar has one configuration.
  std::uint16_t errMode = 0;
  // The document's end: the grammar's own trivia, then the input's end.
  std::uint16_t tailMode = 0;
  Vec<std::uint16_t> modeStart{};
  // What each alphabet admits, and which alphabets cover another: a token read
  // under a wider alphabet answers a narrower one that admits its terminal.
  std::vector<std::uint64_t> modeTerms{};
  std::vector<std::uint64_t> modeSub{};
  std::uint32_t subWords = 0;
  // 1 where any token may be read as several terminals, 2 where only a token
  // that ends the text may.
  Vec<std::uint8_t> modeMulti{};
  // Empty unless a mode can read arbitrarily far beyond its last match.
  std::vector<std::uint8_t> modeBacktrack{};
  // The choices of a mode of the second kind that the end of the text can
  // decide otherwise than their token does.
  std::vector<std::uint8_t> decEnded{};
  std::vector<std::uint16_t> modeTrivia{}, modeJoinedTrivia{};
  std::vector<std::uint16_t> modeLonger{};
  std::uint16_t ndfa = 0;
  Vec<std::uint16_t> dfaNext{};
  std::vector<std::uint32_t> typoSuccBase{};
  std::vector<std::uint16_t> typoSucc{};
  // What a reader of a refused token needs off the automaton.
  std::vector<std::uint8_t> lexBodies{};
  std::vector<std::uint16_t> lexBodyTerms{};
  std::vector<ByteSet> lexOpeners{};
  Vec<std::uint16_t> dfaAccept{};
  Vec<DynSet> dfaAccSet{};
  Vec<DynSet> dfaResSet{};
  Vec<DynSet> dfaRelayResSet{};
  std::vector<std::uint16_t> wordBounds{};
  Vec<std::uint32_t> lexChoiceAt{};
  std::vector<LlLexAccept> lexChoices{};
  std::span<const LlLexAccept> choices(std::size_t s) const {
    return {lexChoices.data() + lexChoiceAt[s * 2], lexChoiceAt[s * 2 + 1]};
  }
  Vec<std::uint8_t> lexChecks{};
  // Per terminal, the words it refuses behind its head and those of them
  // where its token ends, and per mode the terminals refusing some.
  std::vector<HeadMatcher> termHeads{};
  std::vector<HeadMatcher> termTails{};
  std::vector<DynSet> modeHeads{};
  Vec<std::uint16_t> dfaBound{};
  Vec<std::uint16_t> dfaRelay{};
  std::vector<LexRelayGen> relays{};

  // What the static separation left to the simulation, the only ones with a
  // run-time cost: choices no table covers, and tables that end where their
  // arms still share a reading. The parse keeps its stack for either.
  std::uint32_t nadaptive = 0, nopen = 0;
  // Per call site, the run of joins it may need: the chain its call handed
  // back, and the two as one. A site whose call always hands the same record
  // has a run of one and needs no test at all.
  std::vector<std::uint32_t> fuseBase{};
  std::vector<std::uint32_t> ownBase{};
  std::vector<LlFuse> fuse{};
  // Per joined chain, the run the record was given and the run the site stated
  // over it: what fills the node is not what writes it into the region above.
  // `kNoRow` for a chain nobody joined.
  std::vector<LlFuse> origin{};
  // The position each atom edge of the graph belongs to, so a caret can say
  // which element would consume the token it offers.
  std::vector<std::uint16_t> edgeNode{};
  // The call position returning into each state, absent for synthetic calls.
  std::vector<std::uint16_t> returnNode{};
  // The state the graph stands in once each position is read: the one a call
  // returns into.
  std::vector<std::uint32_t> nodeAfter{};
  // Whether a position can be the first its rule's invocation reads.
  std::vector<std::uint8_t> nodeHead{};
  // The skip configuration the trivia in front of a position's token is read
  // under; a loop's first turn apart from the others, and each climb's
  // operator.
  std::vector<std::uint8_t> gapCfg{};
  std::vector<std::uint8_t> headCfg{};
  // A repetition may inspect its distinct exit across the caller's trivia.
  // Its elected body reads its own gap; a nullable call commits its actual
  // entry gap before delegating that decision to its callee.
  std::vector<std::uint8_t> callerPeek{};
  std::vector<std::uint8_t> readHeadCfg{};
  // The configuration the gap after a term is read under, where every reading
  // that may follow agrees, and the alphabet that reads it.
  std::vector<std::uint16_t> afterCfg{};
  std::vector<std::uint16_t> afterGap{};
  // A loop's first turn reads under its own alphabet where its configuration
  // is not the other turns'. Per choice, the mode that first turn is read
  // under, zero for a choice that is no loop; always sized `ndecisions`.
  std::vector<std::uint16_t> headMode{};
  std::vector<std::uint16_t> readHeadMode{}, readMode{};
  // Per choice, whether conditions stand before its token: it is held for
  // every arm as the lexer chose it.
  std::vector<std::uint8_t> decStands{};
  // Per choice, the terminals its alphabet admits for a caller alone that read
  // over another terminal of that alphabet: the descent asks the frames it
  // stands in before it takes one. Empty where there are none.
  std::vector<DynSet> decFlag{};
  // Per state a call returns into, what its caller reads from there on before
  // it ends, and whether it may end reading nothing: what a frame answers when
  // the descent asks it. Empty for every other state, which no frame names.
  std::vector<DynSet> callerReads{};
  std::vector<std::uint8_t> callerEnds{};
  // A lookahead crosses trivia with its choice's error mode, per choice; empty
  // where every choice's is the grammar's.
  std::vector<std::uint16_t> lookMode{};
  // What an episode reads besides the graph, per rule and per choice: see
  // `LlTable`. Empty where none is flagged.
  std::vector<std::uint8_t> sharedRules{};
  std::vector<std::uint8_t> repeatScopes{};
  // Per mode, the mode a fault is lexed with: see `LlTable`. Empty where the
  // grammar has one configuration.
  std::vector<std::uint16_t> errModeOf{};
  // Per section configuration where the grammar embeds others: see
  // `LlTable`. Empty where it embeds none.
  std::vector<std::uint16_t> sectionErr{};
  std::vector<std::uint16_t> sectionEnd{};
  StampLayout stamp{};
  GrammarFault fault{};
  // The rules the fault's cycle runs through, from its own.
  std::vector<std::uint32_t> faultCycle{};
};

}  // namespace cursiv::engine
