// The exact automaton and references to its recovery and derived tables.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cursiv/lr/table/LrBitSet.hpp"
#include "cursiv/lr/table/LrLowCfg.hpp"
#include "cursiv/lr/table/LrSetStore.hpp"
#include "cursiv/lr/table/LrSizes.hpp"
#include "cursiv/lr/table/LrTableFacts.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

template <LrSizes S, const auto& Cold, const auto& Derived>
struct LrFrozenTable : LrTableFacts {
  using TermSet = LrBitSet<S.termBits()>;

  LrLowCfg<S> cfg{};

  std::array<std::uint16_t, S.states> mode{};

  std::array<std::uint16_t, S.modes> modeStart{};
  // A mode where two different terminals can match from the same position
  // with different extents: the cold path must enumerate the candidates.
  std::array<std::uint8_t, S.modes> modeMulti{};
  // Per mode, the winners whose bytes may hide a competitor there: a reading
  // sweeps the position behind them, and only them.
  LrSetStore<S, S.modes, S.modeWinSets> modeWin{};
  // The mode that lexes what a mode takes for the length of its token alone.
  std::array<std::uint16_t, S.modeLongers> modeLonger{};
  static constexpr bool has_longer() { return S.modeLongers != 0; }
  constexpr std::uint16_t longer_mode(std::uint16_t mode) const {
    return modeLonger[mode];
  }
  std::array<std::uint8_t, S.modeBacktracks> modeBacktrack{};
  static constexpr bool has_backtrack() { return S.modeBacktracks != 0; }
  // Per mode, the terminals the lexer takes off a position once no live stack
  // takes them.
  LrSetStore<S, S.modeFlagSets != 0 ? S.modes : 0, S.modeFlagSets> modeFlag{};
  static constexpr bool has_flags() { return S.modeFlagSets != 0; }
  constexpr bool flagged(unsigned mode, SymbolId sym) const {
    if constexpr (S.modeFlagSets == 0)
      return false;
    else
      return modeFlag[static_cast<std::uint16_t>(mode)].test(sym);
  }
  // A mode that crosses no trivia: a region's own.
  std::array<std::uint8_t, S.modes> modeTight{};
  // `from` covers `to`: a token lexed in `from` stands in `to` unless the
  // state has no action for it or `to` flags it in `modeWin`. An indented
  // table has no matrix, and no mode covers another.
  static constexpr std::size_t kSubWords = (S.modes + 63u) / 64u;
  std::array<std::uint64_t, S.indented ? 0 : S.modes * kSubWords> modeSub{};
  // A row of modes per `to`: where they fit one word, a site that names `to`
  // reads a constant.
  constexpr bool mode_sub(unsigned from, unsigned to) const {
    if constexpr (S.indented) {
      return false;
    } else {
      const std::size_t word = kSubWords == 1 ? 0 : from >> 6;
      return ((modeSub[to * kSubWords + word] >> (from & 63u)) & 1u) != 0;
    }
  }

  // A row of classes per state, written as text: the cell past the rows ends
  // it.
  std::array<char16_t, S.dfa * S.cls + 1> dfaNext{};
  // Terminal + 1 ; 0 = not accepting.
  std::array<std::uint16_t, S.dfa> dfaAccept{};
  // Every terminal accepted there, not only the winner.
  LrSetStore<S, S.dfa, S.dfaAccSets> dfaAcc{};
  // Terminals a state's keywords reserve away from an identifier match.
  LrSetStore<S, S.dfa, S.dfaResSets> dfaRes{};
  // What a keyword read behind conditions takes in a state, if it stands.
  LrSetStore<S, S.dfaRelayResSets != 0 ? S.dfa : 0, S.dfaRelayResSets>
      dfaRelayRes{};
  // The class the byte after this match may not be in, or `kNoSet`.
  std::array<std::uint16_t, S.dfa> dfaBound{};
  // The head of a state's relay list, or `kNoRelay`; present only where a token
  // echoes a mark.
  std::array<std::uint16_t, S.relays != 0 ? S.dfa : 0> dfaRelay{};
  std::array<LexRelay, S.relays> relays{};
  static constexpr std::uint16_t nrelays = S.relays;

  // Per mode, where the grammar has more than one configuration: the mode a
  // fault is lexed with.
  std::array<std::uint16_t, S.errModes> errModeOf{};
  // Per mode, where the grammar embeds others: the mode its section's text is
  // read in, whole, and the section's closing token, which a repair never
  // invents, drops or reads as another. `kEofSymbol` elsewhere.
  std::array<std::uint16_t, S.endModes> modeText{};
  std::array<std::uint16_t, S.endModes> modeEnd{};

  constexpr std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    return dfaNext[std::size_t{s} * ncls + cls[b]];
  }
  constexpr bool dfa_bounded(std::uint16_t s) const {
    return dfaBound[s] != kNoSet;
  }

  constexpr const TermSet& dfa_acc_set(std::uint16_t s) const {
    return dfaAcc[s];
  }
  constexpr const TermSet& dfa_res_set(std::uint16_t s) const {
    return dfaRes[s];
  }
  static constexpr TermSet kNoRelayRes{};
  constexpr const TermSet& dfa_relay_res_set(std::uint16_t s) const {
    if constexpr (S.dfaRelayResSets == 0)
      return kNoRelayRes;
    else
      return dfaRelayRes[s];
  }
  constexpr unsigned priority_at(unsigned k) const {
    if constexpr (S.accPriorities == 0)
      return 0;
    else
      return Cold.accPriority[k];
  }
  template <class F>
  constexpr bool each_accept(std::uint16_t s, F&& f) const {
    auto open = dfaAcc[s];
    unsigned k = Cold.accBoundBase[s];
    for (unsigned t = open.take(); t < cfg.nterms; t = open.take(), ++k) {
      if (f(static_cast<SymbolId>(t), Cold.accBound[k], priority_at(k)))
        return true;
      if constexpr (S.accStands != 0)
        if (Cold.accStand[k] != kNoSet &&
            f(static_cast<SymbolId>(t), Cold.accStand[k],
              unsigned{kKeywordPriority}))
          return true;
    }
    return false;
  }

  static constexpr const auto& cold = Cold;
  static constexpr const auto& derived = Derived;

  // Whether a word terminal `t` refuses behind its head is spelled by the
  // token at `p`: a table whose grammar has such a terminal names its own.
  static constexpr bool head_refuses(SymbolId, const char*, TextOffset) {
    return false;
  }
  // How far the words of mode `m`'s refusing terminals spelled whole at `p`
  // are read, as a walk through them reads them.
  static constexpr TextOffset head_reach(unsigned, const char*, TextOffset) {
    return 0;
  }
  // Whether a word terminal `t` refuses where its token ends is spelled at
  // `p`.
  static constexpr bool tail_refuses(SymbolId, const char*, TextOffset) {
    return false;
  }

  // The sets of a word-bounded acceptance, whose terminals reserve. Only a
  // table with a refusing terminal or a section names them: no other takes a
  // keyword off a position.
  static constexpr std::span<const std::uint16_t> wordBounds{};
  static constexpr bool standsWithoutWordClass = true;

  // What the gap decides: a table whose grammar has a guard names its own.
  static constexpr std::span<const std::uint32_t> guardAt{};
  static constexpr std::span<const std::uint16_t> guardOps{};
  static constexpr std::span<const std::uint64_t> held{};

  // The frontier one acceptance declares, `kNoSet` where it declares none;
  // `dfaBound` answers for the priority's winner only.
  constexpr std::uint16_t dfa_acc_bound(std::uint16_t s, unsigned t) const {
    return Cold.accBound[Cold.accBoundBase[s] + this->dfaAcc[s].rank(t)];
  }
};

}  // namespace cursiv::engine
