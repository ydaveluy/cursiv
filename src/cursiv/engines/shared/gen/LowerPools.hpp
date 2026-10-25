// The pools both lowerings write into: text, byte classes, the token graph,
// terminals, gap programs and the configurations a region reads under, each
// growing to meet its write, and the counters that mark where each one ends.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/utils/Tag.hpp"
#include "cursiv/engines/shared/gen/LexWordClass.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/UnboundedCondition.hpp"
#include "cursiv/engines/shared/gen/Vec.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

// What a lowering reads of a terminal and the frozen table never does: beside
// the cell, not in it.
struct TermBuild {
  std::uint16_t lex = 0;  // the token graph
  // The prose a hover shows: it stays in the grammar's own text, the table
  // never carries it.
  std::string_view doc;
  std::uint8_t prec = 0;  // the precedence step this terminal opens
  bool folded = false;
  bool predicateOnly = false;
  Association assoc = Association::left;
  bool charset = false;  // written as a character range

  constexpr bool operator==(const TermBuild&) const = default;
};

struct LowerPools : LowerVerdicts {
  std::uint16_t nameLen = 0;
  std::uint16_t nsets = 0;
  std::uint16_t nlex = 0;
  std::uint16_t nlexKid = 0;
  std::uint16_t nlexTags = 0;
  std::uint16_t ngapConds = 0;
  std::uint16_t ngapLeaves = 0;
  std::uint32_t ngapOps = 0;

  Vec<char> names{};
  Vec<ByteSet> sets{};
  Vec<std::uint16_t> setDecode{};
  // Per decoded set, where `scalarRanges` lists the scalars it tests, as
  // `lex_set_scalars` reads it.
  Vec<std::uint32_t> setScalars{};
  Vec<unicode::Range> scalarRanges{};
  Vec<unicode::Range> wordRanges{};
  Vec<LexNode> lex{};
  Vec<std::uint16_t> lexKid{};
  // The mode a condition's node lexes its token in, by its node.
  Vec<std::uint16_t> lexMode{};
  // A refused word as the grammar's dump spells it, by its node.
  Vec<std::string> lexSpelled{};
  Vec<const detail::Tag*> lexTags{};
  Vec<TermInfo> terms{};
  Vec<TermBuild> termBuild{};
  // Condition `k` is the program `gapOps[gapCondAt[k], gapCondAt[k + 1])`.
  Vec<GapLeaf> gapLeaves{};
  Vec<std::uint16_t> gapOps{};
  Vec<std::uint32_t> gapCondAt{};
  // The conditions a completion cannot step over, one finding per rule and
  // spelled run.
  std::vector<UnboundedCondition> unbounded{};
  // Configuration `k` skips `cfgTrivia[cfgTriviaAt[k], cfgTriviaAt[k + 1])`;
  // zero is the grammar's.
  Vec<std::uint16_t> cfgTrivia{};
  Vec<std::uint32_t> cfgTriviaAt{};
  // The section configuration `k` stands in, zero outside any; and for a
  // section's own, the token that closes it.
  Vec<std::uint8_t> cfgHome{};
  Vec<std::uint16_t> cfgEnd{};
  // Configuration `k` names no trivia at all (bare), and reads as a token
  // some terminal the grammar skips (short).
  Vec<std::uint8_t> cfgBare{};
  Vec<std::uint8_t> cfgShort{};
  // The terminal a region's own symbol reads in another role, zero where a
  // symbol is its terminal's own.
  Vec<std::uint16_t> cloneBase{};
  // The word class of each section's grammar that is not the generated
  // grammar's own.
  Vec<LexWordClass> sectionWords{};
  // The bounds of a keyword's acceptances that add refusals of their own to
  // the word class: the keyword stands where none of them follows.
  Vec<std::uint16_t> keywordBounds{};
  // The readings a region asks for, and those already read.
  Vec<std::uint8_t> cloneNeeded{};
  Vec<std::uint8_t> cloneDone{};

  // A new condition, the conjunction of `conds`: a copy where there is one.
  // `kNoRow` where no id is left.
  std::uint16_t gap_conjoin(std::span<const std::uint16_t> conds) {
    if (ngapConds >= kGapConds) {
      lower_refuse(*this, LowerFail::gapTooMany);
      return kNoRow;
    }
    const auto out = static_cast<std::uint16_t>(ngapConds);
    gapCondAt[out] = ngapOps;
    for (const std::uint16_t k : conds)
      for (std::uint32_t i = gapCondAt[k]; i < gapCondAt[k + 1u]; ++i) {
        // Read by offset: the append may move the program under a reference.
        const std::uint16_t v = gapOps[i];
        gapOps[ngapOps++] = v;
      }
    if (conds.size() > 1) {
      gapOps[ngapOps++] = kGapAnd;
      gapOps[ngapOps++] = static_cast<std::uint16_t>(conds.size());
    }
    ++ngapConds;
    gapCondAt[ngapConds] = ngapOps;
    return out;
  }

  std::uint16_t push_lex(LexNode n) {
    if (nlex == 0xffffu) throw LowerRefused(LowerFail::tokenGraphTooLarge);
    lex[nlex] = n;
    return nlex++;
  }

  std::uint16_t push_lex_kid(std::uint16_t k) {
    if (nlexKid == 0xffffu) throw LowerRefused(LowerFail::tokenGraphTooLarge);
    lexKid[nlexKid] = k;
    return nlexKid++;
  }

  // A tag is interned by its address; a mark and an echo carry the index.
  std::uint16_t intern_tag(const detail::Tag& tag) {
    for (std::uint16_t i = 0; i < nlexTags; ++i)
      if (lexTags[i] == &tag) return i;
    if (nlexTags == 0xffffu) throw LowerRefused(LowerFail::tokenGraphTooLarge);
    lexTags[nlexTags] = &tag;
    return nlexTags++;
  }

  // A section's class, once: the grammars of two sections may state one.
  void note_section_word(LexWordClass w) {
    if (std::ranges::none_of(sectionWords.v, [&](const LexWordClass& o) {
          return o.bytes == w.bytes && o.wide == w.wide;
        }))
      sectionWords[sectionWords.size()] = std::move(w);
  }

  bool sections() const {
    return std::ranges::any_of(cfgHome.v,
                               [](std::uint8_t h) { return h != 0; });
  }

  std::string_view term_name(std::uint16_t t) const {
    return name_view(terms[t].nameOff, terms[t].nameLen);
  }
  std::string_view term_text(std::uint16_t t) const {
    return name_view(terms[t].textOff, terms[t].textLen);
  }

 protected:
  // A refused declaration may still be printed: a name that did not fit in
  // the pool reads empty.
  std::string_view name_view(std::uint16_t off, std::uint16_t len) const {
    if (len == 0 || off >= nameLen || len > nameLen - off) return {};
    return {names.data() + off, len};
  }
};

}  // namespace cursiv
