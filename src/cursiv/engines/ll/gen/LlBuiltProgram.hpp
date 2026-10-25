// What the construction adds to a lowered program: the graph a prediction
// walks, what every position may begin and be followed by, and the verdict of
// every choice. The program itself travels through unchanged.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/ll/gen/LlLowCfg.hpp"
#include "cursiv/engines/ll/table/LlAtn.hpp"
#include "cursiv/engines/ll/table/LlDfa.hpp"
#include "cursiv/engines/ll/table/LlFuse.hpp"
#include "cursiv/engines/ll/table/LlLexAccept.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/HeadMatcher.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/ShapeOverflow.hpp"
#include "cursiv/engines/shared/gen/Vec.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

// A run of joins a site reads from `kFuse` rather than spelling it where it
// stands: more than one, or any a group rides.
constexpr bool ll_fuse_tabled(std::uint32_t joins, bool rides) {
  return joins > (rides ? 0u : 1u);
}

struct LlBuiltProgram : ShapeOverflow {
  LlLowCfg cfg{};
  ChainPool writes{};
  std::vector<std::uint8_t> nestChains{};

  // The graph, one state per position of a body and one edge per way out.
  Vec<LlAtnState> states{};
  std::uint32_t nstates = 0;
  Vec<LlAtnEdge> edges{};
  std::uint32_t nedges = 0;
  std::vector<std::uint32_t> ruleStart{};
  std::vector<std::uint32_t> ruleStop{};
  std::vector<std::uint32_t> callerBase{};
  std::vector<std::uint32_t> callerAt{};
  std::vector<std::uint32_t> returnClass{};

  // Per position, then per rule: what may begin here, what may follow, and
  // whether nothing at all is a reading of it.
  std::vector<DynSet> first{};
  std::vector<DynSet> after{};
  std::vector<std::uint8_t> nullable{};
  std::vector<DynSet> ruleFirst{};
  std::vector<DynSet> ruleFollow{};
  std::vector<std::uint8_t> ruleNullable{};
  // The rules the entry reaches, a climb's operators and operand included.
  std::vector<bool> liveRules{};

  // Per condition, the tokens it refuses where the token in hand alone decides
  // it -- a commit's exit -- and none for any other.
  std::vector<DynSet> cut{};
  // One row per arm of every decision, which is what a switch reads.
  std::vector<DynSet> laSet{};
  // ...and the automata the exploration closed, all of them in one pool.
  std::vector<LlDfaState> dfa{};
  std::vector<LlDfaEdge> dfaEdge{};
  // Where the document is entered, and where it ends: the entry rule has a
  // caller like every other, or a closure could not read past its own end.
  std::uint32_t atnEntry = 0;

  // The lexer: the byte classes every edge is written in, one automaton for
  // all the modes at once, and where each mode starts in it.
  std::uint8_t cls[256]{};
  std::uint8_t ncls = 0;
  std::uint16_t nmodes = 0;
  // The mode no position owns: it lexes every terminal, so a repair can name
  // the token the parse could not even read.
  std::uint16_t errMode = 0;
  // The document's end: the grammar's own trivia, then the input's end.
  std::uint16_t tailMode = 0;
  Vec<std::uint16_t> modeStart{};
  // What each alphabet admits, and which alphabets another one covers: a
  // token read under a wider alphabet answers a narrower one whenever the
  // narrower admits the terminal it matched, so no point re-reads what the
  // point above it already read.
  std::vector<std::uint64_t> modeTerms{};
  std::vector<std::uint64_t> modeSub{};
  std::uint32_t subWords = 0;
  // 1 where any token may be read as several terminals, 2 where only a token
  // that ends the text may; for the latter, the terminals that meet there.
  Vec<std::uint8_t> modeMulti{};
  std::vector<DynSet> modeTie{};
  // The choices of a mode of the second kind that the end of the text can
  // settle otherwise than their token does.
  std::vector<std::uint8_t> decEnded{};
  std::vector<std::uint16_t> modeTrivia{}, modeJoinedTrivia{};
  std::vector<std::uint16_t> modeLonger{};
  std::uint16_t ndfa = 0;
  Vec<std::uint16_t> dfaNext{};
  std::vector<std::uint32_t> typoSuccBase{};
  std::vector<std::uint16_t> typoSucc{};
  // What a reader of a refused token needs off the automaton.
  std::vector<std::uint8_t> lexBodies{};
  std::vector<std::uint16_t> lexOwed{};
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

  // What the static separation left to the simulation, the only numbers that
  // cost at run time: choices no table covers, and tables that end where their
  // arms still share a reading. The parse keeps its stack for either.
  std::uint32_t nadaptive = 0, nopen = 0;
  // Per call site, the run of joins it may need: the chain its call handed
  // back, and the two as one. A site whose call always hands the same record
  // has a run of one and needs no test at all.
  std::vector<std::uint32_t> fuseBase{};
  std::vector<std::uint32_t> ownBase{};
  std::vector<LlFuse> fuse{};
  // Where a joined chain came from, per chain: the run the record was handed
  // and the run the site stated over it. A reader of the tree needs the two
  // apart -- what fills the node the record built is not what writes that node
  // into the region above. `kNoRow` for a chain nobody joined.
  std::vector<LlFuse> origin{};
  // The state a call returns into, per call position of the program.
  std::vector<std::uint32_t> nodeFollow{};
  // The position each atom edge of the graph belongs to, so a caret can say
  // which element would consume the token it offers.
  std::vector<std::uint16_t> edgeNode{};
  // The call position returning into each state, absent for synthetic calls.
  std::vector<std::uint16_t> returnNode{};
  // The state the graph stands in once each position is read, and the
  // alphabet that position was read under. A climb's operator is a position
  // too, though the program holds no node for it: its own run follows the
  // nodes'.
  std::vector<std::uint32_t> nodeAfter{};
  std::vector<std::uint16_t> nodeAfterMode{};
  // Whether a position can be the first its rule's invocation reads.
  std::vector<std::uint8_t> nodeHead{};
  // The skip configuration the trivia in front of a position's token is read
  // under; a loop's first turn apart from the others, and each climb's
  // operator.
  std::vector<std::uint8_t> gapCfg{};
  std::vector<std::uint8_t> headCfg{};
  std::vector<std::uint8_t> climbCfg{};
  // The configuration the gap after a term is read under, where every reading
  // that may follow agrees, and the alphabet that reads it.
  std::vector<std::uint16_t> afterCfg{};
  std::vector<std::uint16_t> afterGap{};
  // A loop's first turn reads under its own alphabet where its configuration
  // is not the other turns'. A lookahead crosses trivia with its choice's
  // error mode; empty where every choice's is the grammar's.
  std::vector<std::uint16_t> headMode{};
  // Per choice, whether conditions stand before its token: it is held for
  // every arm as the lexer chose it.
  std::vector<std::uint8_t> decStands{};
  std::vector<std::uint16_t> lookMode{};
  // Per section configuration where the grammar embeds others: see
  // `LlTable`. Empty where it embeds none.
  std::vector<std::uint16_t> sectionErr{};
  std::vector<std::uint16_t> sectionEnd{};
  StampLayout stamp{};
  GrammarFault fault{};
  // The rules the fault's cycle runs through, from its own.
  std::vector<std::uint32_t> faultCycle{};
};

}  // namespace cursiv
