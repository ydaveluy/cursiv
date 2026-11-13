#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/GapReach.hpp"
#include "cursiv/shared/gen/LexModes.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/GapLeaf.hpp"
#include "cursiv/shared/table/LexNode.hpp"

namespace cursiv::engine {

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

  // Folds `root`'s first terminals and nullability, its children first.
  void read(std::uint16_t root) {
    using enum LexKind;
    for (std::vector todo{std::pair{root, false}}; !todo.empty();) {
      const auto [at, folds] = todo.back();
      todo.pop_back();
      const LexNode e = g.lex[at];
      if (!folds) {
        if (done[at]) continue;
        done[at] = true;
        todo.emplace_back(at, true);
        if (e.kind == seq || e.kind == alt) {
          for (unsigned i = e.b; i-- > 0;)
            todo.emplace_back(g.lexChild[e.a + i], false);
        } else if (e.kind == rep || e.kind == positive || e.kind == negative) {
          todo.emplace_back(e.a, false);
        }
        continue;
      }
      nullable[at] = true;
      if (e.kind == token) {
        first[at].set(e.a);
        nullable[at] = false;
      } else if (e.kind == rep) {
        first[at] = first[e.a];
        nullable[at] = e.min == 0 || nullable[e.a];
      } else if (e.kind == seq || e.kind == alt) {
        nullable[at] = e.kind == seq;
        for (unsigned i = 0; i < e.b; ++i) {
          const auto child = g.lexChild[e.a + i];
          if (e.kind == alt || nullable[at]) first[at] |= first[child];
          nullable[at] = e.kind == alt ? nullable[at] || nullable[child]
                                       : nullable[at] && nullable[child];
        }
      }
    }
  }

  std::vector<bool> trace() {
    std::vector<bool> reached(g.nlex);
    std::vector<std::pair<std::uint16_t, DynSet>> todo;
    const auto visit = [&](std::uint16_t root, const DynSet& from) {
      using enum LexKind;
      todo.emplace_back(root, from);
      while (!todo.empty()) {
        const auto [at, after] = std::move(todo.back());
        todo.pop_back();
        if (reached[at] && follow[at].covers(after)) continue;
        reached[at] = true;
        follow[at] |= after;
        read(at);
        const LexNode e = g.lex[at];
        if (e.kind == seq) {
          DynSet next = after;
          for (unsigned i = e.b; i-- > 0;) {
            const auto child = g.lexChild[e.a + i];
            todo.emplace_back(child, next);
            if (nullable[child])
              next |= first[child];
            else
              next = first[child];
          }
        } else if (e.kind == alt) {
          for (unsigned i = 0; i < e.b; ++i)
            todo.emplace_back(g.lexChild[e.a + i], after);
        } else if (e.kind == rep) {
          todo.emplace_back(e.a, first[e.a] | after);
          // A round's skipper also elects against the reading that stops.
          if (e.b != kNoLexNode) todo.emplace_back(e.b, first[e.a] | after);
        } else if (e.kind == positive || e.kind == negative) {
          todo.emplace_back(e.a, DynSet{});
        } else if (e.kind == skip) {
          for (unsigned i = 0; i < e.b; ++i) read(g.lexChild[e.a + i]);
        }
      }
    };
    for (const auto& leaf : g.gapLeaves.v)
      if (leaf.reads_tokens()) {
        visit(leaf.node, DynSet{});
        if (leaf.skip != kNoLexNode) visit(leaf.skip, first[leaf.node]);
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
      for (unsigned i = 0; i < e.b; ++i) out |= first[g.lexChild[e.a + i]];
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
    for (const unsigned u : want.members())
      if (u != 0 && spelled_twin(cfg, t, u)) return true;
    return false;
  };
  for (const unsigned t : first.members())
    if (t != 0 && !want.test(t) && !cfg.terms[t].trivia && !twin_wanted(t))
      out.set(t);
  return out;
}

// Sets in `cfg` the mode each predicate node that lexes reads its token in:
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

}  // namespace cursiv::engine
