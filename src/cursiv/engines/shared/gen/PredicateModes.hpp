#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/gen/GapReach.hpp"
#include "cursiv/engines/shared/gen/LexModes.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

template <class Cfg>
struct PredicateModes {
  explicit PredicateModes(const Cfg& cfg)
      : g(cfg),
        first(cfg.nlex),
        follow(cfg.nlex),
        nullable(cfg.nlex),
        done(cfg.nlex) {}

  const Cfg& g;
  std::vector<DynSet> first, follow;
  std::vector<bool> nullable, done;

  void read(std::uint16_t at) {
    if (done[at]) return;
    done[at] = true;
    const LexNode e = g.lex[at];
    nullable[at] = true;
    if (e.kind == LexKind::token) {
      first[at].set(e.a);
      nullable[at] = false;
    } else if (e.kind == LexKind::rep) {
      read(e.a);
      first[at] = first[e.a];
      nullable[at] = e.min == 0 || nullable[e.a];
    } else if (e.kind == LexKind::seq || e.kind == LexKind::alt) {
      nullable[at] = e.kind == LexKind::seq;
      for (unsigned i = 0; i < e.b; ++i) {
        const auto kid = g.lexKid[e.a + i];
        read(kid);
        if (e.kind == LexKind::alt || nullable[at]) first[at] |= first[kid];
        nullable[at] = e.kind == LexKind::alt ? nullable[at] || nullable[kid]
                                              : nullable[at] && nullable[kid];
      }
    } else if (e.kind == LexKind::positive || e.kind == LexKind::negative) {
      read(e.a);
    }
  }

  std::vector<bool> trace() {
    std::vector<bool> reached(g.nlex);
    const auto visit = [&](auto&& self, std::uint16_t at,
                           const DynSet& after) -> void {
      if (reached[at] && follow[at].covers(after)) return;
      reached[at] = true;
      follow[at] |= after;
      read(at);
      const LexNode e = g.lex[at];
      if (e.kind == LexKind::seq) {
        DynSet next = after;
        for (unsigned i = e.b; i-- > 0;) {
          const auto kid = g.lexKid[e.a + i];
          self(self, kid, next);
          if (nullable[kid])
            next |= first[kid];
          else
            next = first[kid];
        }
      } else if (e.kind == LexKind::alt) {
        for (unsigned i = 0; i < e.b; ++i) self(self, g.lexKid[e.a + i], after);
      } else if (e.kind == LexKind::rep) {
        self(self, e.a, first[e.a] | after);
        // A round's skipper also elects against the reading that stops.
        if (e.b != kNoLexNode) self(self, e.b, first[e.a] | after);
      } else if (e.kind == LexKind::positive || e.kind == LexKind::negative) {
        self(self, e.a, DynSet{});
      } else if (e.kind == LexKind::skip) {
        for (unsigned i = 0; i < e.b; ++i) read(g.lexKid[e.a + i]);
      }
    };
    for (const auto& leaf : g.gapLeaves.v)
      if (leaf.reads_tokens()) {
        visit(visit, leaf.node, DynSet{});
        if (leaf.skip != kNoLexNode) visit(visit, leaf.skip, first[leaf.node]);
      }
    return reached;
  }

  static bool lexes(LexKind k) {
    return k == LexKind::token || k == LexKind::alt || k == LexKind::rep ||
           k == LexKind::skip;
  }
  // The terminals a traced node that lexes reads its token among.
  DynSet want(std::uint16_t at) const {
    const LexNode& e = g.lex[at];
    if (e.kind == LexKind::skip) {
      DynSet out = follow[at];
      for (unsigned i = 0; i < e.b; ++i) out |= first[g.lexKid[e.a + i]];
      return out;
    }
    DynSet out = first[at];
    if (e.kind == LexKind::rep || nullable[at])
      out |= follow[at];
    return out;
  }
};

// Per condition, the terminals its token-reading leaves can read first, but for
// a commit's exit, whose first token the rule reads.
template <class Cfg>
std::vector<DynSet> condition_firsts(const Cfg& cfg) {
  PredicateModes<Cfg> m(cfg);
  const GapReach<Cfg> reach(cfg);
  std::vector<DynSet> out(cfg.ngapConds);
  for (unsigned k = 0; k < cfg.ngapConds; ++k)
    for (const std::uint16_t v : reach.leaves(k)) {
      const GapLeaf& leaf = cfg.gapLeaves[v];
      if (!leaf.reads_tokens() || leaf.trigger) continue;
      m.read(leaf.node);
      out[k] |= m.first[leaf.node];
    }
  return out;
}

// Whether `u` spells the text a spelling only a condition names (`t`), as a
// grammar terminal.
template <class Cfg>
bool spelled_twin(const Cfg& cfg, unsigned t, unsigned u) {
  return u != t && cfg.terms[u].spelled && !cfg.termBuild[u].predicateOnly &&
         cfg.termBuild[u].folded == cfg.termBuild[t].folded &&
         cfg.term_text(static_cast<std::uint16_t>(u)) ==
             cfg.term_text(static_cast<std::uint16_t>(t));
}

// The grammar terminals of the text a spelling only a condition names.
template <class Cfg>
DynSet spelled_twins(const Cfg& cfg, unsigned t) {
  DynSet out;
  if (!cfg.termBuild[t].predicateOnly || !cfg.terms[t].spelled) return out;
  for (unsigned u = 1; u < cfg.nterms; ++u)
    if (spelled_twin(cfg, t, u)) out.set(u);
  return out;
}

// The terminals the conditions standing at a point read first that the point
// does not read: they lex for the length of the token and nothing else.
template <class Cfg>
DynSet longer_firsts(const Cfg& cfg, const DynSet& want, const DynSet& first) {
  DynSet out;
  const auto twin_wanted = [&](unsigned t) {
    if (!cfg.termBuild[t].predicateOnly || !cfg.terms[t].spelled) return false;
    for (unsigned u = 1; u < cfg.nterms; ++u)
      if (want.test(u) && spelled_twin(cfg, t, u)) return true;
    return false;
  };
  for (unsigned t = 1; t < cfg.nterms; ++t)
    if (first.test(t) && !want.test(t) && !cfg.terms[t].trivia &&
        !twin_wanted(t))
      out.set(t);
  return out;
}

// Lays in `cfg` the mode each predicate node that lexes reads its token in:
// what it wants, with no terminal lexed for length, so a condition reads the
// walk's token alone.
template <class Cfg>
void predicate_modes(Cfg& cfg, LexModes& modes) {
  PredicateModes<Cfg> m(cfg);
  const auto reached = m.trace();
  for (std::uint16_t at = 0; at < cfg.nlex; ++at)
    if (reached[at] && m.lexes(cfg.lex[at].kind))
      cfg.lexMode[at] = modes.intern(m.want(at));
}

}  // namespace cursiv
