// The exact automaton and references to its recovery and derived tables.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cursiv/engines/lr/table/LrBitSet.hpp"
#include "cursiv/engines/lr/table/LrLowCfg.hpp"
#include "cursiv/engines/lr/table/LrLowSizes.hpp"
#include "cursiv/engines/lr/table/LrSetStore.hpp"
#include "cursiv/engines/lr/table/LrSizes.hpp"
#include "cursiv/engines/lr/table/LrTableFacts.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <LrSizes S, LrLowSizes C, const auto& Cold, const auto& Derived>
struct LrFrozenTable : LrTableFacts {
  using TermSet = LrBitSet<S.termBits()>;

  LrLowCfg<C> cfg{};

  std::array<std::uint16_t, S.states> mode{};

  std::array<std::uint16_t, S.modes> modeStart{};
  // A mode where two different terminals can match from the same position
  // with different extents: the cold path must enumerate the candidates.
  std::array<std::uint8_t, S.modes> modeMulti{};
  // Per mode, the winners whose bytes may hide a rival there: a reading
  // sweeps the position behind them, and only them.
  LrSetStore<S, S.modes, S.modeWinSets> modeWin{};
  // The mode that lexes what a mode takes for the length of its token alone.
  std::array<std::uint16_t, S.modeLongers> modeLonger{};
  static constexpr bool has_longer() { return S.modeLongers != 0; }
  constexpr std::uint16_t longer_mode(std::uint16_t mode) const {
    return modeLonger[mode];
  }
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
  // state has no action for it or `to` flags it in `modeWin`.
  std::array<std::uint64_t,
             (std::size_t{S.modes} * S.modes + 63) / 64>
      modeSub{};
  constexpr bool mode_sub(unsigned from, unsigned to) const {
    const auto i = std::size_t{from} * S.modes + to;
    return ((modeSub[i >> 6] >> (i & 63u)) & 1u) != 0;
  }

  std::array<std::uint16_t, S.dfa * S.cls> dfaNext{};
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
  // The head of a state's relay list, or `kNoRelay`; laid only where a token
  // echoes a mark.
  std::array<std::uint16_t, S.relays != 0 ? S.dfa : 0>
      dfaRelay{};
  std::array<LexRelay, S.relays> relays{};
  static constexpr std::uint16_t nrelays = S.relays;

  // Where the grammar embeds others, per mode: the mode a fault is lexed
  // with -- in a section, its grammar's terminals and the token that closes
  // it -- and that token where the state reads inside the section, which a
  // repair never invents, drops or reads as another. `kEofSymbol` elsewhere.
  std::array<std::uint16_t, S.errModes> errModeOf{};
  std::array<std::uint16_t, S.errModes> modeEnd{};

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
  // The tier `t` reads at `s`: a keyword's reading kept beside it ranks it.
  constexpr unsigned dfa_tier(std::uint16_t s, unsigned t) const {
    auto out = 0u;
    each_accept(s, [&](SymbolId term, std::uint16_t, unsigned tier) {
      if (term == t && tier > out) out = tier;
      return false;
    });
    return out;
  }
  constexpr unsigned tier_at(unsigned k) const {
    if constexpr (S.accTiers == 0)
      return 0;
    else
      return Cold.accTier[k];
  }
  template <class F>
  constexpr bool each_accept(std::uint16_t s, F&& f) const {
    auto open = dfaAcc[s];
    unsigned k = Cold.accBoundBase[s];
    for (unsigned t = open.take(); t < cfg.nterms; t = open.take(), ++k) {
      if (f(static_cast<SymbolId>(t), Cold.accBound[k], tier_at(k)))
        return true;
      if constexpr (S.accStands != 0)
        if (Cold.accStand[k] != kNoSet &&
            f(static_cast<SymbolId>(t), Cold.accStand[k],
              unsigned{kTierKeyword}))
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

}  // namespace cursiv
