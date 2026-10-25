// The exact extent of one built table: the only dimension the frozen table
// has.
#pragma once

namespace cursiv {

struct LrSizes {
  unsigned terms = 0, prods = 0;
  unsigned states = 0, modes = 0, cls = 0, dfa = 0;
  unsigned dfaAccSets = 0, dfaResSets = 0, modeWinSets = 0;
  // A mode per mode where one takes terminals for the length of its token
  // alone, none where no mode does.
  unsigned modeLongers = 0;
  // The sets of terminals modes take off a position once no live stack takes
  // them, none where no mode does.
  unsigned modeFlagSets = 0;
  unsigned accBounds = 0;
  unsigned relays = 0;
  unsigned closeSets = 0;
  // Zero where the grammar has no keyword read behind conditions.
  unsigned dfaRelayResSets = 0;
  // Zero where no terminal keeps a word-bounded reading beside an unbounded
  // one.
  unsigned accStands = 0;
  // Zero where no terminal is read as a literal or a keyword.
  unsigned accTiers = 0;
  // A mode per mode where the grammar embeds others, none where it does not.
  unsigned errModes = 0;
  // The lists of the written order of insertions, and their terminals.
  unsigned insertLists = 0, insertWords = 0;
  unsigned electWords = 0, electWatches = 0, electChains = 0,
           electChainWords = 0, electTwins = 0, electRhsSyms = 0;
  // The 64-bit words of the bits that say which cells the table orders as the
  // election does.
  unsigned orderWords = 0;

  // A bit domain of zero bits has no word to live in.
  constexpr unsigned termBits() const { return terms ? terms : 1; }

  constexpr bool operator==(const LrSizes&) const = default;
};

}  // namespace cursiv
