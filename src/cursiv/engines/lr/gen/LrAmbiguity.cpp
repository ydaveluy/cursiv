#include "cursiv/engines/lr/gen/LrAmbiguity.hpp"

#include <algorithm>

#include "cursiv/engines/lr/gen/LrBuckets.hpp"
#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

namespace {

// A production no condition or end assertion has a say in.
bool plain(const LrBuildCfg& c, std::uint16_t k) {
  const LrProdBuild& b = c.prodBuild[k];
  return c.prods[k].gap == 0 && !b.endOnly && b.stop == 0;
}

bool spells_only(const LrBuildCfg& c, const LrLowProd& q, std::uint16_t sym) {
  for (unsigned i = 0; i < q.len; ++i)
    if (c.sym_at(q, i) != sym) return false;
  return true;
}

// The item of each nonterminal that lowers a repetition, `R -> R I` beside
// `R -> I ... I`: every run of I is a string of R, so two runs side by side are
// one too.
std::vector<std::uint16_t> find_runs(const LrBuildCfg& c,
                                     const LrBuckets<std::uint16_t>& ix) {
  std::vector<std::uint16_t> runs(c.nnts, kNoRow);
  for (std::uint16_t nt = 0; nt < c.nnts; ++nt) {
    for (const std::uint16_t k : ix.of(nt)) {
      const LrLowProd& q = c.prods[k];
      if (q.len != 2 || !plain(c, k) || c.sym_at(q, 0) != nt_sym(nt)) continue;
      const std::uint16_t item = c.sym_at(q, 1);
      const bool based = std::ranges::any_of(ix.of(nt), [&](std::uint16_t b) {
        return c.prods[b].len != 0 && plain(c, b) &&
               spells_only(c, c.prods[b], item);
      });
      if (!based) continue;
      runs[nt] = item;
      break;
    }
  }
  return runs;
}

// A symbol as an author reads it: an auxiliary is told by what it derives,
// its first alternatives and two levels down.
std::string plain_name(const LrBuildCfg& c, std::uint16_t sym, unsigned depth) {
  if (is_term(sym)) return std::string(c.term_name(sym));
  const std::uint16_t nt = nt_index(sym);
  if (nt < c.nnts - c.auxCount) return std::string(c.nt_name(nt));
  if (depth == 0) return "...";
  std::string out = "(";
  unsigned shown = 0;
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    if (q.lhs != nt) continue;
    if (shown == 3) {
      out += " | ...";
      break;
    }
    if (shown++ != 0) out += " | ";
    for (unsigned i = 0; i < q.len; ++i) {
      if (i != 0) out += ' ';
      out += plain_name(c, c.sym_at(q, i), depth - 1);
    }
    if (q.len == 0) out += "nothing";
  }
  return out + ")";
}

std::string spell(const LrBuildCfg& c, std::uint16_t sym) {
  return "`" + plain_name(c, sym, 2) + "`";
}

}  // namespace

std::vector<LrAmbiguity> lr_find_ambiguities(const LrBuildCfg& c) {
  const auto ix = lr_by_lhs(c);
  const std::vector<bool> live = lr_reachable(c, ix);
  const std::vector<std::uint16_t> runs = find_runs(c, ix);
  std::vector<LrAmbiguity> found;
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    if (!live[q.lhs] || !plain(c, k)) continue;
    // Two runs of one item next to each other: the run splits between them.
    for (unsigned i = 0; i + 1 < q.len; ++i) {
      const std::uint16_t x = c.sym_at(q, i);
      const std::uint16_t y = c.sym_at(q, i + 1);
      if (is_term(x) || is_term(y)) continue;
      const std::uint16_t item = runs[nt_index(x)];
      if (item == kNoRow || item != runs[nt_index(y)]) continue;
      const LrAmbiguity a{c.prodBuild[k].owner, item};
      if (std::ranges::find(found, a) == found.end()) found.push_back(a);
    }
  }
  return found;
}

std::string lr_ambiguity_text(const LrBuildCfg& c, const LrAmbiguity& a) {
  const std::string who = a.rule < c.nnts
                              ? "rule `" + std::string(c.nt_name(a.rule)) + "` "
                              : "a rule of this grammar ";
  const std::string item = spell(c, a.item);
  return who + "reads a run of " + item +
         " twice in a row: the run splits between the two in several ways -- "
         "write one repetition";
}

}  // namespace cursiv
