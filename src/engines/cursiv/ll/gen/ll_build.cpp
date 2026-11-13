// Analyses a lowered program: first and follow sets, the graph a prediction
// walks, and how each choice is decided. It names no grammar type, so it
// compiles once for every grammar.
#include "cursiv/ll/gen/ll_build.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/ll/gen/LlRecordPlan.hpp"
#include "cursiv/ll/gen/ll_lex.hpp"
#include "cursiv/ll/runtime/LlEpisodeSim.hpp"
#include "cursiv/ll/table/LlSimulation.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/LexSeparation.hpp"
#include "cursiv/shared/gen/PredicateModes.hpp"
#include "cursiv/shared/gen/bit_words.hpp"
#include "cursiv/utils/StrongComponents.hpp"
#include "cursiv/shared/table/GapLeaf.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/utils/RoundMarks.hpp"

namespace cursiv::engine {
namespace {

using Built = LlBuiltProgram;
using Cfg = LlLowCfg;

// Queues an element at most once until its next visit. A grown fact requeues
// only its direct readers, not the whole grammar.
struct Pending {
  std::deque<std::uint32_t> todo;
  std::vector<std::uint8_t> queued;

  explicit Pending(std::size_t size) : queued(size, 0) {}
  void push(std::uint32_t id) {
    if (queued[id]) return;
    queued[id] = 1;
    todo.push_back(id);
  }
  bool empty() const { return todo.empty(); }
  std::uint32_t pop() {
    const auto id = todo.front();
    todo.pop_front();
    queued[id] = 0;
    return id;
  }
};

constexpr std::uint32_t kNoElement = ~std::uint32_t{0};

// An element is a position, or `nnodes` plus a rule. What a rule's element
// reads its facts from: its body, or the rule of a climb's operand;
// `kNoElement` where it has neither.
std::uint32_t rule_source(const Cfg& c, std::uint32_t id) {
  const LlRuleInfo& u = c.rules[id - c.nnodes];
  if (u.climb != kNoRow) return c.nnodes + c.climbs[u.climb].primary;
  return u.body != kNoRow ? u.body : kNoElement;
}

// Who reads each element, and the order the analyses visit them in: positions
// before their groups, then rules. One graph serves every analysis.
struct ShapeGraph {
  std::vector<std::vector<std::uint32_t>> users;
  std::vector<std::uint32_t> order;

  explicit ShapeGraph(const Cfg& c) : users(c.nnodes + c.nrules) {
    for (std::uint32_t id = 0; id < c.nnodes; ++id) {
      const LlNode& n = c.nodes[id];
      if (n.kind == LlShape::call)
        users[c.nnodes + n.sym].push_back(id);
      else
        for (const auto child : c.children(n)) users[child].push_back(id);
    }
    for (std::uint32_t id = c.nnodes + 1; id < users.size(); ++id)
      if (const auto from = rule_source(c, id); from != kNoElement)
        users[from].push_back(id);
    // Each element comes after what it reads, as far as cycles allow, so a fact
    // reaches its reader whole. The fixpoint does not depend on the order.
    const auto cycles = StrongComponents<>::of(users);
    order.resize(users.size());
    std::iota(order.begin(), order.end(), 0u);
    std::ranges::stable_sort(
        order, std::ranges::greater{},
        [&](std::uint32_t id) { return cycles.component[id]; });
    // Rule zero has no body.
    std::erase(order, c.nnodes);
  }
};

template <class Update>
void propagate_shapes(const ShapeGraph& g, std::uint64_t& work,
                      Update&& update) {
  Pending pending(g.users.size());
  for (const std::uint32_t id : g.order) pending.push(id);
  while (!pending.empty()) {
    const auto id = pending.pop();
    ++work;
    if (update(id))
      for (const auto user : g.users[id]) pending.push(user);
  }
}

bool element_nullable(const Cfg& c, std::uint32_t id,
                      std::span<const std::uint8_t> nullable,
                      bool endMarkNullable) {
  // A climb with no operator in the text is its operand alone.
  if (id >= c.nnodes) {
    const auto from = rule_source(c, id);
    return from != kNoElement && nullable[from] != 0;
  }
  switch (const LlNode& n = c.nodes[id]; n.kind) {
    using enum LlShape;
    case term:
      return false;
    // The end assertion reads no token but refuses any input that has one:
    // its lookahead starts with `$end`, so it is not empty.
    case endMark:
      return endMarkNullable;
    case empty:
    case condition:
    case implied:
      return true;
    case call:
      return nullable[c.nnodes + n.sym] != 0;
    case seq:
      return std::ranges::all_of(c.children(n),
                                 [&](auto k) { return nullable[k] != 0; });
    case alt:
      return std::ranges::any_of(c.children(n),
                                 [&](auto k) { return nullable[k] != 0; });
    case rep:
      return n.min == 0 || nullable[n.sym] != 0;
  }
  return false;
}

// Whether each element can read nothing.
std::vector<std::uint8_t> compute_nullable(const Cfg& c,
                                           const ShapeGraph& shapes,
                                           std::uint64_t& work,
                                           bool endMarkNullable = false) {
  std::vector<std::uint8_t> nullable(c.nnodes + c.nrules, 0);
  propagate_shapes(shapes, work, [&](std::uint32_t id) {
    if (nullable[id] || !element_nullable(c, id, nullable, endMarkNullable))
      return false;
    nullable[id] = 1;
    return true;
  });
  return nullable;
}

// Adds `t` to `set`; true when it was new.
bool put(DynSet& set, unsigned t) {
  if (set.test(t)) return false;
  set.set(t);
  return true;
}

// What stands at the head of each element: a group's members up to the first
// of a sequence that must read, a call's rule, and what `leaf` puts for any
// other position.
template <class Leaf>
std::vector<DynSet> head_sets(const Cfg& c, const ShapeGraph& shapes,
                              std::span<const std::uint8_t> nullable,
                              std::uint64_t& work, Leaf&& leaf) {
  std::vector<DynSet> heads(c.nnodes + c.nrules);
  propagate_shapes(shapes, work, [&](std::uint32_t id) {
    DynSet& out = heads[id];
    if (id >= c.nnodes) {
      const auto from = rule_source(c, id);
      return from != kNoElement && out.add(heads[from]);
    }
    const LlNode& n = c.nodes[id];
    switch (n.kind) {
      using enum LlShape;
      case call:
        return out.add(heads[c.nnodes + n.sym]);
      case seq:
      case alt:
      case rep: {
        bool grew = false;
        for (const auto k : c.children(n)) {
          grew = out.add(heads[k]) || grew;
          if (n.kind == seq && !nullable[k]) break;
        }
        return grew;
      }
      default:
        return leaf(n, out);
    }
  });
  return heads;
}

void compute_first(Built& b, const ShapeGraph& shapes, std::uint64_t& work) {
  const Cfg& c = b.cfg;
  // What can start an implied token's condition: the tokens it stands before.
  std::vector<DynSet> impliedHeads(c.ngapConds);
  const std::vector<DynSet> firsts = condition_firsts(c);
  for (unsigned k = 0; k < c.ngapConds; ++k)
    for (const unsigned t : firsts[k].members()) {
      if (t == 0) continue;
      if (c.termBuild[t].predicateOnly)
        impliedHeads[k] |= spelled_twins(c, t);
      else
        impliedHeads[k].set(t);
    }
  const auto leaf = [&](const LlNode& n, DynSet& out) {
    switch (n.kind) {
      using enum LlShape;
      case term:
        return put(out, n.sym);
      case endMark: {
        const bool eof = put(out, kEofSymbol);
        return put(out, n.sym) || eof;
      }
      case implied:
        return out.add(impliedHeads[n.min]);
      default:
        return false;
    }
  };
  b.first = head_sets(c, shapes, b.nullable, work, leaf);
}

// The conditions no cut decides that stand at the head of each element.
void compute_lead(Built& b, const ShapeGraph& shapes, std::uint64_t& work) {
  const auto leaf = [&](const LlNode& n, DynSet& out) {
    return n.kind == LlShape::condition && b.cut[n.sym].none() &&
           put(out, n.sym);
  };
  b.lead = head_sets(b.cfg, shapes, b.nullable, work, leaf);
}

// The rules this position may call before it consumes a token; a cycle here is
// left recursion. Iterative, since a counted repetition nests as deep as its
// bound.
void left_calls(const Built& b, const std::vector<std::uint8_t>& empty,
                std::uint16_t root, std::vector<bool>& seen,
                std::vector<std::uint16_t>& out) {
  std::vector todo{root};
  while (!todo.empty()) {
    const LlNode& n = b.cfg.nodes[todo.back()];
    todo.pop_back();
    if (n.kind == LlShape::call && !seen[n.sym]) {
      seen[n.sym] = true;
      out.push_back(n.sym);
    }
    // A sequence reaches its members up to the first that consumes. The
    // members go on the stack last first, so they come off in text order.
    const auto childIds = b.cfg.children(n);
    std::size_t reach = childIds.size();
    if (n.kind == LlShape::seq)
      for (std::size_t i = 0; i < childIds.size(); ++i)
        if (!empty[childIds[i]]) {
          reach = i + 1;
          break;
        }
    todo.insert(todo.end(),
                std::make_reverse_iterator(childIds.begin() + reach),
                std::make_reverse_iterator(childIds.begin()));
  }
}

// A call into `r`'s own cycle that sits in a repetition or after an optional
// element: the lowering expands such alternatives only up to the optional
// limit, so the refusal names that limit.
bool repeated_lead(const Built& b, const std::vector<std::uint8_t>& empty,
                   std::uint16_t r, std::span<const std::uint32_t> component) {
  const Cfg& c = b.cfg;
  struct At {
    std::uint16_t id;
    bool repeated;
  };
  std::vector<At> todo;
  if (c.rules[r].body != kNoRow) todo.push_back({c.rules[r].body, false});
  while (!todo.empty()) {
    auto [id, repeated] = todo.back();
    todo.pop_back();
    const LlNode& n = c.nodes[id];
    if (n.kind == LlShape::call) {
      if (repeated && component[n.sym] == component[r]) return true;
      continue;
    }
    repeated = repeated || n.kind == LlShape::rep;
    for (const auto k : c.children(n)) {
      todo.push_back({k, repeated});
      if (n.kind != LlShape::seq) continue;
      if (!empty[k]) break;
      repeated = repeated || c.nodes[k].kind == LlShape::rep;
    }
  }
  return false;
}

void refuse_left_recursion(Built& b, const std::vector<std::uint8_t>& empty) {
  const std::uint16_t n = b.cfg.nrules;
  std::vector<std::vector<std::uint16_t>> edge(n);
  std::vector<bool> seen(n, false);
  for (std::uint16_t r = 1; r < n; ++r) {
    if (const LlRuleInfo& u = b.cfg.rules[r]; u.climb != kNoRow)
      edge[r].push_back(b.cfg.climbs[u.climb].primary);
    else if (u.body != kNoRow)
      left_calls(b, empty, u.body, seen, edge[r]);
    for (const auto q : edge[r]) seen[q] = false;
  }
  const auto scc = StrongComponents<>::of(edge);
  const auto calls = [&](std::uint32_t q, auto&& to) {
    for (const auto x : edge[q]) to(x);
  };
  for (std::uint16_t r = 1; r < n; ++r) {
    if (!scc.cyclic[r]) continue;
    b.faultCycle = scc.walk(r, r, calls);
    const bool repeated = repeated_lead(b, empty, r, scc.component);
    b.fault.raise(GrammarFaultCode::leftRecursion, r, repeated,
                  repeated ? std::uint16_t{kOptionalGate} : std::uint16_t{0});
    return;
  }
}

std::vector<bool> reached_from(const Cfg& c, std::uint16_t from) {
  std::vector<bool> seen(c.nrules, false);
  if (from >= c.nrules) return seen;
  std::vector<std::uint16_t> rules{from}, nodes;
  seen[from] = true;
  const auto rule = [&](std::uint16_t r) {
    if (r < c.nrules && !seen[r]) {
      seen[r] = true;
      rules.push_back(r);
    }
  };
  while (!rules.empty()) {
    const LlRuleInfo& u = c.rules[rules.back()];
    rules.pop_back();
    if (u.body != kNoRow) nodes.push_back(u.body);
    if (u.climb != kNoRow) {
      const LlClimb& cl = c.climbs[u.climb];
      rule(cl.primary);
      if (cl.selectNode != kNoRow) nodes.push_back(cl.selectNode);
    }
    while (!nodes.empty()) {
      const LlNode& n = c.nodes[nodes.back()];
      nodes.pop_back();
      if (n.kind == LlShape::call) rule(n.sym);
      for (const auto k : c.children(n)) nodes.push_back(k);
    }
  }
  return seen;
}

// Whether a position of a live rule names a terminal that is skipped there,
// so the lexer never returns it. Iterative, since a counted repetition nests as
// deep as its bound.
bool names_skipped(const Cfg& c, std::uint16_t id) {
  std::vector nodes{id};
  while (!nodes.empty()) {
    const LlNode& n = c.nodes[nodes.back()];
    nodes.pop_back();
    if (n.kind == LlShape::term) {
      if (c.terms[n.sym].trivia) return true;
      continue;
    }
    for (const auto k : c.children(n)) nodes.push_back(k);
  }
  return false;
}

void refuse_skipped_terminal(Built& b) {
  const Cfg& c = b.cfg;
  for (std::uint16_t i = 1; i < c.nrules; ++i) {
    if (!b.liveRules[i]) continue;
    const LlRuleInfo& u = c.rules[i];
    bool named = u.body != kNoRow && names_skipped(c, u.body);
    if (u.climb != kNoRow) {
      const LlClimb& cl = c.climbs[u.climb];
      named =
          named || (cl.selectNode != kNoRow && names_skipped(c, cl.selectNode));
    }
    if (named) b.fault.raise(GrammarFaultCode::skippedTerminal, i);
  }
}

// Refuses a rule whose every reading reads a token after its own `eof`, or
// waits on such a rule: it derives no finite text. Only a token of the same
// sequence as the `eof` counts.
void refuse_unproductive(Built& b, const ShapeGraph& shapes,
                         std::uint64_t& work) {
  const Cfg& c = b.cfg;
  std::vector<std::uint8_t> productive(c.nnodes + c.nrules, 0);
  const auto rule = [&](std::uint16_t r) {
    return productive[c.nnodes + r] != 0;
  };
  const auto derives = [&](std::uint32_t id) {
    if (id >= c.nnodes) {
      const auto from = rule_source(c, id);
      return from != kNoElement && productive[from] != 0;
    }
    switch (const LlNode& n = c.nodes[id]; n.kind) {
      using enum LlShape;
      case call:
        return rule(n.sym);
      case seq: {
        const auto k = c.children(n);
        bool ended = false;
        for (const auto child : k) {
          const LlShape s = c.nodes[child].kind;
          if (!productive[child] || (ended && (s == term || s == implied)))
            return false;
          ended = ended || s == endMark;
        }
        return true;
      }
      case alt:
        return std::ranges::any_of(c.children(n),
                                   [&](auto k) { return productive[k] != 0; });
      case rep:
        return n.min == 0 || productive[n.sym] != 0;
      default:
        return true;
    }
  };
  propagate_shapes(shapes, work, [&](std::uint32_t id) {
    if (productive[id] || !derives(id)) return false;
    productive[id] = 1;
    return true;
  });
  // The rule to name is the one the others wait on.
  const auto waits_on = [&](std::uint16_t r) {
    const LlRuleInfo& u = c.rules[r];
    std::vector<std::uint16_t> nodes;
    if (u.climb != kNoRow) {
      const std::uint16_t primary = c.climbs[u.climb].primary;
      return rule(primary) ? kNoRow : primary;
    }
    if (u.body != kNoRow) nodes.push_back(u.body);
    while (!nodes.empty()) {
      const LlNode& n = c.nodes[nodes.back()];
      nodes.pop_back();
      if (n.kind == LlShape::call && !rule(n.sym)) return n.sym;
      const auto childIds = c.children(n);
      nodes.insert(nodes.end(), childIds.rbegin(), childIds.rend());
    }
    return kNoRow;
  };
  for (std::uint16_t r = 1; r < c.nrules; ++r) {
    if (!b.liveRules[r] || rule(r)) continue;
    std::vector<bool> seen(c.nrules, false);
    std::uint16_t at = r;
    for (;;) {
      seen[at] = true;
      const std::uint16_t next = waits_on(at);
      if (next == kNoRow || seen[next]) break;
      at = next;
    }
    b.fault.raise(GrammarFaultCode::ruleWithoutProduction, at);
    return;
  }
}

// The skip configuration of the trivia before each token: that of the group
// holding both tokens. Every gap one point reads must agree. One bit per
// configuration. Every fact is per element.
struct Gaps {
  const Built& b;
  std::vector<std::uint8_t> silent{}, consumes{}, freshOut{};
  // Whether some empty derivation reads a condition. A nullable call can
  // delegate an optional exit only when no empty path reads one.
  std::vector<std::uint8_t> emptyReads{};
  // The gaps before its first token from a token that nothing peeked after,
  // the gaps across it where it reads nothing, and the gaps after its last
  // token.
  std::vector<DynSet> pre{}, span{}, after{};
  // Whether a callee's first positions take the gaps of its callers.
  bool entries = true;
  // Per sequence, one past its last member that consumes and one past its last
  // that may not be passed. Fixed once the shape is.
  std::vector<std::uint32_t> consumeEnd{}, passEnd{};

  static DynSet bit(unsigned k) {
    DynSet s;
    s.set(k);
    return s;
  }
  unsigned cfg(std::uint16_t id) const { return b.cfg.nodes[id].cfg; }
  unsigned section(std::uint16_t id) const { return b.cfg.nodes[id].section; }
  unsigned rule_cfg(std::uint16_t r) const { return b.cfg.ruleCfg[r]; }
  std::uint32_t rule(std::uint16_t r) const { return b.cfg.nnodes + r; }
  const LlNode& node(std::uint16_t id) const { return b.cfg.nodes[id]; }
  std::uint16_t child(const LlNode& n, unsigned i) const {
    return b.cfg.childIds[n.sym + i];
  }

  // Whether a position may pass without peeking, may consume, and may end
  // on a token with no peek after it: least fixpoints, a rule reaching itself
  // before it consumes being refused.
  void compute_shape(const ShapeGraph& shapes, std::uint64_t& work) {
    propagate_shapes(shapes, work, [&](std::uint32_t id) {
      bool moved = false;
      const auto raise = [&](std::vector<std::uint8_t>& v, bool x) {
        if (x && !v[id]) {
          v[id] = 1;
          moved = true;
        }
      };
      if (id >= b.cfg.nnodes) {
        if (const LlRuleInfo& u = b.cfg.rules[id - b.cfg.nnodes];
            u.climb != kNoRow) {
          raise(consumes, true);
          raise(emptyReads, emptyReads[rule(b.cfg.climbs[u.climb].primary)]);
        } else if (u.body != kNoRow) {
          raise(silent, silent[u.body]);
          raise(consumes, consumes[u.body]);
          raise(freshOut, freshOut[u.body]);
          raise(emptyReads, emptyReads[u.body]);
        }
        return moved;
      }
      const LlNode& n = node(static_cast<std::uint16_t>(id));
      bool si = false, co = false, fr = false, er = false;
      switch (n.kind) {
        case LlShape::term:
          co = fr = true;
          break;
        case LlShape::empty:
          si = true;
          break;
        case LlShape::endMark:
          // Zero width, yet the table reads its gap as a token's.
          co = true;
          break;
        case LlShape::condition:
        case LlShape::implied:
          er = true;
          break;
        case LlShape::call:
          si = silent[rule(n.sym)];
          co = consumes[rule(n.sym)];
          fr = freshOut[rule(n.sym)];
          er = emptyReads[rule(n.sym)];
          break;
        case LlShape::seq: {
          si = true;
          std::uint16_t last = kNoRow;
          for (unsigned i = 0; i < n.count; ++i) {
            const std::uint16_t k = child(n, i);
            si = si && silent[k];
            co = co || consumes[k];
            if (b.nullable[id]) er = er || emptyReads[k];
            if (!silent[k]) last = k;
          }
          fr = last != kNoRow && freshOut[last];
          break;
        }
        case LlShape::alt:
          for (unsigned i = 0; i < n.count; ++i) {
            co = co || consumes[child(n, i)];
            fr = fr || freshOut[child(n, i)];
            if (b.nullable[child(n, i)]) er = er || emptyReads[child(n, i)];
          }
          break;
        case LlShape::rep:
          si = n.min != 0 && silent[n.sym];
          co = consumes[n.sym];
          fr = n.max != kEndless && freshOut[n.sym];
          er = n.max != 0 && b.nullable[n.sym] && emptyReads[n.sym];
          break;
      }
      raise(silent, si);
      raise(consumes, co);
      raise(freshOut, fr);
      raise(emptyReads, er);
      return moved;
    });
  }

  void note_later() {
    consumeEnd.assign(b.cfg.nnodes, 0);
    passEnd.assign(b.cfg.nnodes, 0);
    for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
      const LlNode& n = node(static_cast<std::uint16_t>(id));
      if (n.kind != LlShape::seq) continue;
      for (unsigned i = 0; i < n.count; ++i) {
        const std::uint16_t k = child(n, i);
        if (consumes[k]) consumeEnd[id] = i + 1;
        if (!b.nullable[k]) passEnd[id] = i + 1;
      }
    }
  }

  // The gap sets over the shape facts `g` holds: both passes read one shape.
  void compute_sets(const Gaps& g, std::uint64_t& work) {
    Pending pending(b.cfg.nnodes + b.cfg.nrules);
    const auto add = [&](DynSet& to, const DynSet& x, std::uint32_t target) {
      if (to.add(x)) pending.push(target);
    };
    for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) pending.push(rule(r));
    for (std::uint32_t at = b.cfg.nnodes; at-- > 0;) pending.push(at);
    while (!pending.empty()) {
      const auto at = pending.pop();
      ++work;
      if (at >= b.cfg.nnodes) {
        const auto r = static_cast<std::uint16_t>(at - b.cfg.nnodes);
        if (const LlRuleInfo& u = b.cfg.rules[r]; u.climb != kNoRow) {
          // An operand stands after the operator it follows, and before the
          // operator or the end of the climb.
          const LlClimb& cl = b.cfg.climbs[u.climb];
          const DynSet own = bit(rule_cfg(r));
          const DynSet in = pre[at] | own;
          const DynSet out = after[at] | own;
          add(pre[rule(cl.primary)], in, rule(cl.primary));
          add(after[rule(cl.primary)], out, rule(cl.primary));
          add(pre[cl.selectNode], out, cl.selectNode);
          add(span[cl.selectNode], own, cl.selectNode);
          add(after[cl.selectNode], own, cl.selectNode);
        } else if (u.body != kNoRow) {
          add(pre[u.body], pre[at], u.body);
          add(span[u.body], span[at], u.body);
          add(after[u.body], after[at], u.body);
        }
        continue;
      }
      const auto id = static_cast<std::uint16_t>(at);
      const LlNode& n = node(id);
      switch (n.kind) {
        case LlShape::call:
          if (entries) {
            add(pre[rule(n.sym)], pre[id], rule(n.sym));
            add(span[rule(n.sym)], span[id], rule(n.sym));
          }
          add(after[rule(n.sym)], after[id], rule(n.sym));
          break;
        case LlShape::alt:
          // The gap in front of an arm's first token is the choice's.
          for (unsigned i = 0; i < n.count; ++i) {
            add(pre[child(n, i)], pre[id], child(n, i));
            add(span[child(n, i)], span[id], child(n, i));
            add(after[child(n, i)], after[id], child(n, i));
          }
          break;
        case LlShape::rep: {
          const DynSet own = bit(cfg(id));
          const std::uint16_t k = n.sym;
          if (n.min >= 1) {
            add(pre[k], pre[id], k);
            add(span[k], span[id], k);
          }
          add(after[k], (n.max > 1 ? own : DynSet{}) | after[id], k);
          break;
        }
        case LlShape::seq: {
          const DynSet own = bit(cfg(id));
          // The gap in front of member `i`: a section's own on both its
          // sides.
          const auto gap = [&](unsigned i) {
            if (i < n.count && section(child(n, i)) != 0)
              return bit(section(child(n, i)) - 1u);
            if (i > 0 && section(child(n, i - 1)) != 0)
              return bit(section(child(n, i - 1)) - 1u);
            return own;
          };
          bool fresh = false, above = true;
          for (unsigned i = 0; i < n.count; ++i) {
            const std::uint16_t k = child(n, i);
            const bool in = g.consumeEnd[id] > i + 1,
                       out = g.passEnd[id] <= i + 1;
            if (fresh) {
              add(pre[k], gap(i), k);
              add(span[k],
                  (in ? gap(i) : DynSet{}) | (out ? after[id] : DynSet{}), k);
            }
            if (above) {
              add(pre[k], pre[id], k);
              add(span[k],
                  (in ? pre[id] : DynSet{}) | (out ? span[id] : DynSet{}), k);
            }
            add(after[k],
                (in ? gap(i + 1) : DynSet{}) | (out ? after[id] : DynSet{}), k);
            if (!g.silent[k]) {
              fresh = g.freshOut[k] != 0;
              above = false;
            }
          }
          break;
        }
        default:
          break;
      }
    }
  }
};

void compute_gaps(Built& b, const ShapeGraph& shapes,
                  const LexAlphabet& alphabet, LlBuildWork& work) {
  const std::uint32_t nn = b.cfg.nnodes;
  const std::size_t elements = nn + b.cfg.nrules;
  b.gapCfg.assign(nn, 0);
  b.headCfg.assign(nn, 0);
  b.callerPeek.assign(nn, 0);
  b.readHeadCfg.assign(nn, 0);
  for (std::uint32_t id = 0; id < nn; ++id)
    b.gapCfg[id] = b.headCfg[id] = b.readHeadCfg[id] = b.cfg.nodes[id].cfg;
  if (b.cfg.cfgTriviaAt.size() <= 2) return;
  Gaps g{b};
  g.silent = g.consumes = g.freshOut = g.emptyReads =
      std::vector<std::uint8_t>(elements, 0);
  g.compute_shape(shapes, work.gapShape);
  g.note_later();
  // The document opens and ends under the grammar's own configuration.
  const auto compute_gap_sets = [&](Gaps& x) {
    x.pre.assign(elements, DynSet{});
    x.span.assign(elements, DynSet{});
    x.after.assign(elements, DynSet{});
    const auto entry = x.rule(b.cfg.entryRule);
    x.pre[entry] = x.span[entry] = x.after[entry] = Gaps::bit(0);
    x.compute_sets(g, work.gapSets);
  };
  compute_gap_sets(g);
  // A call site peeks its callee's first token: callers that cross the gap
  // in front of it under different configurations share no point inside it.
  Gaps local{.b = b, .entries = false};
  compute_gap_sets(local);

  const auto lowest = [](const DynSet& s) { return *s.members().begin(); };
  const auto one = [&](const DynSet& set, const DynSet& own,
                       std::uint8_t& out) {
    if (set.none()) return;
    const DynSet* at = &set;
    if (set.more_than_one()) {
      for (const unsigned k : set.members())
        if (b.cfg.section_cfg_at(lowest(set)) != b.cfg.section_cfg_at(k)) {
          lower_refuse(b.cfg, LowerFail::sectionPointShared);
          return;
        }
      if (own.more_than_one())
        lower_refuse(b.cfg, LowerFail::regionBoundaryShared);
      if (own.none()) return;
      at = &own;
    }
    out = static_cast<std::uint8_t>(lowest(*at));
  };
  // A position that may read nothing peeks the token behind it too.
  const auto peeked = [&](const Gaps& x, std::uint16_t id) {
    return x.pre[id] | (b.nullable[id] ? x.span[id] : DynSet{});
  };
  LexSeparation separated(b.cfg, alphabet);
  const auto separated_after = [&](std::uint16_t id, const DynSet& first) {
    if (b.after[id].none() || b.after[id].test(kEofSymbol)) return false;
    const auto own = g.cfg(id);
    const DynSet& configs = g.after[id];
    bool same = true;
    for (const unsigned k : configs.members())
      same = same && k + 1u < b.cfg.cfgTriviaAt.size() &&
             b.cfg.section_cfg_at(k) == b.cfg.section_cfg_at(own);
    if (!same || first.none() || !separated.disjoint(first, b.after[id]))
      return false;
    for (const unsigned k : configs.members())
      same = same && separated.peek_safe(first, own, k);
    return same;
  };
  for (std::uint32_t at = 0; at < nn; ++at) {
    const auto id = static_cast<std::uint16_t>(at);
    const LlNode& n = b.cfg.nodes[id];
    switch (n.kind) {
      case LlShape::term:
      case LlShape::alt:
        one(peeked(g, id), peeked(local, id), b.gapCfg[id]);
        break;
      case LlShape::call:
        // A nullable callee chooses itself between its body and the caller's
        // trivia, so its gaps are not mixed here. Every empty exit must be free
        // of conditions.
        if (b.nullable[id] && !g.emptyReads[id] && g.freshOut[id] &&
            separated_after(id, b.first[id])) {
          one(g.pre[id], local.pre[id], b.gapCfg[id]);
          b.callerPeek[id] = 1;
        } else {
          one(peeked(g, id), peeked(local, id), b.gapCfg[id]);
        }
        break;
      case LlShape::endMark:
        break;
      case LlShape::condition:
      case LlShape::implied:
        one(g.span[id], local.span[id], b.gapCfg[id]);
        break;
      case LlShape::rep: {
        // A nullable body's first token and the token after its empty arm
        // need not cross the same trivia. Keep the consuming entry apart
        // from the decision, which may inspect the caller's closing token.
        one(g.pre[id], local.pre[id], b.readHeadCfg[id]);
        const bool separate =
            ((b.nullable[id] && peeked(g, id).more_than_one()) ||
             (n.max > 1 && g.freshOut[n.sym] &&
              (Gaps::bit(g.cfg(id)) | local.after[id]).more_than_one())) &&
            separated_after(id, b.first[n.sym]);
        if (b.nullable[id] && separate) {
          one(g.after[id], local.after[id], b.headCfg[id]);
          b.callerPeek[id] = 1;
        } else {
          one(peeked(g, id), peeked(local, id), b.headCfg[id]);
        }
        if (n.max > 1 && g.freshOut[n.sym]) {
          // A loop may inspect its distinct closing token across the
          // caller's trivia. Its next item still consumes under the region's
          // own mode, so trivia between two items is never silently skipped.
          if (separate) {
            one(g.after[id], local.after[id], b.gapCfg[id]);
            b.callerPeek[id] = 1;
          } else {
            one(Gaps::bit(g.cfg(id)) | g.after[id],
                Gaps::bit(g.cfg(id)) | local.after[id], b.gapCfg[id]);
          }
        }
        break;
      }
      default:
        break;
    }
  }
  b.afterCfg.assign(nn, kNoRow);
  for (std::uint32_t id = 0; id < nn; ++id) {
    const DynSet& set = g.after[id];
    if (b.cfg.nodes[id].kind == LlShape::term && set.any() &&
        !set.more_than_one())
      b.afterCfg[id] = static_cast<std::uint16_t>(lowest(set));
  }
}

// A choice read past its first token, where an arm may open on the token in
// front of a section: its lookahead would read the section's first token
// across the caller's trivia.
void refuse_section_lookahead(Built& b) {
  DynSet opens;
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    if (n.kind != LlShape::seq) continue;
    for (unsigned i = 1; i < n.count; ++i) {
      if (b.cfg.nodes[b.cfg.childIds[n.sym + i]].section == 0) continue;
      // Only a terminal opener is checked: any other opener ends on the last
      // token it reads, and no last set is built.
      const LlNode& open = b.cfg.nodes[b.cfg.childIds[n.sym + i - 1]];
      if (open.kind == LlShape::term) opens.set(open.sym);
    }
  }
  if (opens.none()) return;
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    const LlDecision& dec = b.cfg.decisions[d];
    if (dec.how == LlPredict::single) continue;
    for (unsigned a = 0; a < dec.nalts; ++a)
      if (b.laSet[dec.set + a].intersects(opens))
        lower_refuse(b.cfg, LowerFail::sectionPointShared);
  }
}

// What follows every position, to a fixpoint: a rule's own follow feeds the
// positions that call it, and those feed it back. Only `ruleFollow` feeds the
// propagation, so only its growth asks for another pass.
struct Follow {
  const Cfg& c;
  const std::vector<std::uint8_t>& nullable;
  const std::vector<DynSet>& first;
  std::vector<DynSet>& after;
  std::vector<DynSet>& ruleFollow;
  // A rule's walk reads only its own FOLLOW: it is walked again when that grows.
  Pending grown;

  void walk(std::uint16_t id, const DynSet& set) {
    struct Visit {
      std::uint16_t id;
      DynSet follow;
    };
    std::vector<Visit> pending{{id, set}};
    while (!pending.empty()) {
      Visit visit = std::move(pending.back());
      pending.pop_back();
      after[visit.id].add(visit.follow);
      const LlNode& n = c.nodes[visit.id];
      switch (n.kind) {
        using enum LlShape;
        case call:
          if (ruleFollow[n.sym].add(visit.follow)) grown.push(n.sym);
          break;
        case seq: {
          const auto k = c.children(n);
          DynSet acc = std::move(visit.follow);
          const auto begin = pending.size();
          for (auto i = static_cast<unsigned>(k.size()); i-- > 0;) {
            pending.emplace_back(k[i], acc);
            if (!nullable[k[i]]) acc = DynSet{};
            acc |= first[k[i]];
          }
          // Visit the members right to left.
          std::reverse(pending.begin() + begin, pending.end());
          break;
        }
        case alt:
        case rep: {
          // A turn is followed by the next one, or whatever follows the loop.
          DynSet inner = std::move(visit.follow);
          if (n.kind == rep && n.max > 1) inner |= first[n.sym];
          const auto children = c.children(n);
          for (auto k = children.rbegin(); k != children.rend(); ++k)
            pending.emplace_back(*k, inner);
          break;
        }
        default:
          break;
      }
    }
  }
};

void compute_after(const Cfg& c, const std::vector<std::uint8_t>& nullable,
                   const std::vector<DynSet>& first, std::vector<DynSet>& after,
                   std::vector<DynSet>& ruleFollow, std::uint64_t& work) {
  after.assign(c.nnodes, DynSet{});
  ruleFollow.assign(c.nrules, DynSet{});
  // The document ends after the entry, and nothing else may.
  ruleFollow[c.entryRule].set(kEofSymbol);
  Follow f{c, nullable, first, after, ruleFollow, Pending(c.nrules)};
  for (std::uint16_t r = 1; r < c.nrules; ++r) f.grown.push(r);
  while (!f.grown.empty()) {
    const auto r = static_cast<std::uint16_t>(f.grown.pop());
    ++work;
    const LlRuleInfo& u = c.rules[r];
    if (u.climb != kNoRow) {
      // An operand is followed by an operator of the table, or by whatever
      // follows the climb.
      const LlClimb& cl = c.climbs[u.climb];
      DynSet ops = ruleFollow[r];
      for (unsigned i = 0; i < cl.opCount; ++i)
        ops |= first[c.climbOps[cl.opBase + i].body];
      if (ruleFollow[cl.primary].add(ops)) f.grown.push(cl.primary);
      after[cl.selectNode].add(ruleFollow[r]);
      const auto& select = c.nodes[cl.selectNode];
      for (unsigned i = 0; i < cl.opCount; ++i)
        f.walk(c.childIds[select.sym + i], first[c.nnodes + cl.primary]);
      f.walk(c.childIds[select.sym + cl.opCount], ruleFollow[r]);
    } else if (u.body != kNoRow) {
      f.walk(u.body, ruleFollow[r]);
    }
  }
}

// The graph, built from the program: one state per boundary of a body, one
// edge per way out of it. A decision's own state leaves exactly one edge per
// arm, in the arms' order, so a closure names the arm by the edge it took.
struct AtnGen {
  Built& b;
  std::vector<std::vector<LlAtnEdge>> out{};
  std::vector<std::vector<std::uint16_t>> owner{};
  // The states a call already returns into: only a second call to the same
  // place adds a state.
  std::vector<std::uint8_t> claimed{};

  std::uint32_t add(LlAtnKind k, std::uint16_t rule,
                    std::uint16_t dec = kNoRow) {
    const auto id = static_cast<std::uint32_t>(out.size());
    out.emplace_back();
    owner.emplace_back();
    claimed.push_back(0);
    b.returnNode.push_back(kNoRow);
    b.states[id] = LlAtnState{.kind = k, .rule = rule};
    b.nstates = id + 1;
    if (dec != kNoRow) choose(id, dec);
    return id;
  }
  // The decision's own state, which is what a simulation starts from. A
  // choice leaves from the position it stands on, which nothing leaves yet:
  // a walk then crosses no state that only passes it on.
  void choose(std::uint32_t at, std::uint16_t dec) {
    b.states[at].decision = dec;
    b.cfg.decisions[dec].state = at;
  }
  void eps(std::uint32_t from, std::uint32_t to) {
    out[from].push_back(LlAtnEdge{.kind = LlEdgeKind::epsilon, .target = to});
    owner[from].push_back(kNoRow);
  }
  void atom(std::uint32_t from, std::uint16_t t, std::uint32_t to,
            std::uint16_t node = kNoRow) {
    out[from].push_back(
        LlAtnEdge{.kind = LlEdgeKind::atom, .label = t, .target = to});
    owner[from].push_back(node);
    if (node != kNoRow) b.nodeAfter[node] = to;
  }
  // A return state no other call shares, the call position kept beside the
  // edge: a caret under the callee needs the member this call writes. Only a
  // second claim adds an epsilon.
  void call(std::uint32_t from, std::uint16_t r, std::uint32_t to,
            std::uint16_t node = kNoRow) {
    std::uint32_t back = to;
    if (claimed[to] != 0) {
      back = add(LlAtnKind::basic, b.states[from].rule);
      eps(back, to);
      claimed[back] = 1;
    } else {
      claimed[to] = 1;
    }
    out[from].push_back(LlAtnEdge{.kind = LlEdgeKind::rule,
                                  .label = r,
                                  .target = b.ruleStart[r],
                                  .follow = back});
    owner[from].push_back(node);
    b.returnNode[back] = node;
  }

  void build(std::uint16_t id, std::uint32_t from, std::uint32_t to,
             std::uint16_t rule);
};

void AtnGen::build(std::uint16_t id, std::uint32_t from, std::uint32_t to,
                   std::uint16_t rule) {
  struct Visit {
    std::uint16_t id;
    std::uint32_t from, to;
    unsigned child = 0;
  };
  // Iterative: counted options nest deeply. States, edges and calls are
  // numbered in depth-first order; a visit with no node adds the epsilon that
  // follows a completed child.
  std::vector<Visit> pending{{id, from, to}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    id = visit.id;
    from = visit.from;
    to = visit.to;
    if (id == kNoRow) {
      eps(from, to);
      continue;
    }
    const LlNode& n = b.cfg.nodes[id];
    switch (n.kind) {
      case LlShape::term:
        atom(from, n.sym, to, id);
        break;
      // The assertion consumes nothing and stands only where the text ends,
      // which is what `$end` in front of it says (or in a section, the token
      // that closes it). Its edges name the assertion, which is no token's.
      case LlShape::endMark:
        atom(from, kEofSymbol, to, id);
        if (n.sym != kEofSymbol) atom(from, n.sym, to, id);
        break;
      case LlShape::empty:
        eps(from, to);
        break;
      case LlShape::condition:
      case LlShape::implied:
        out[from].push_back(
            LlAtnEdge{.kind = LlEdgeKind::condition,
                      .label = static_cast<std::uint16_t>(
                          n.kind == LlShape::condition ? n.sym : n.min),
                      .target = to});
        owner[from].push_back(id);
        b.nodeAfter[id] = to;
        break;
      case LlShape::call:
        b.nodeAfter[id] = to;
        call(from, n.sym, to, id);
        break;
      case LlShape::seq: {
        if (const auto k = b.cfg.children(n); k.empty()) {
          eps(from, to);
        } else {
          const unsigned i = visit.child;
          const std::uint32_t next =
              i + 1 == k.size() ? to : add(LlAtnKind::basic, rule);
          if (i + 1 < k.size()) pending.push_back({id, next, to, i + 1});
          pending.push_back({k[i], from, next});
        }
        break;
      }
      case LlShape::alt: {
        const auto k = b.cfg.children(n);
        choose(from, n.decision);
        const std::uint32_t tail = add(LlAtnKind::basic, rule);
        pending.push_back({kNoRow, tail, to});
        const auto begin = pending.size();
        for (const auto child : k) {
          const auto arm = add(LlAtnKind::basic, rule);
          eps(from, arm);
          pending.push_back({child, arm, tail});
        }
        std::reverse(pending.begin() + begin, pending.end());
        break;
      }
      case LlShape::rep: {
        // Each counted occurrence already has its own position. Options have
        // no back edge; an unbounded repetition returns to its choice instead.
        if (n.min == 0) choose(from, n.decision);
        const auto body = add(LlAtnKind::basic, rule);
        const auto back = add(LlAtnKind::basic, rule,
                              n.min == 0 ? kNoRow : n.decision);
        if (n.min == 0) {
          eps(from, body);
          eps(from, to);
          pending.push_back({kNoRow, back, n.max == 1 ? to : from});
        } else {
          // One turn is required; arm 0 takes another after the body completes.
          eps(from, body);
          pending.push_back({kNoRow, back, to});
          pending.push_back({kNoRow, back, body});
        }
        pending.push_back({n.sym, body, back});
        break;
      }
    }
  }
}

// A climb is `primary (op primary)*`. The graph decides its loop by the
// operator alone; the descent checks the operator's level.
void build_climb(AtnGen& g, std::uint16_t rule, LlClimb& cl, std::uint32_t from,
                 std::uint32_t to) {
  using enum LlAtnKind;
  Built& b = g.b;
  const std::uint32_t entry =
      g.add(basic, rule, b.cfg.nodes[cl.selectNode].decision);
  cl.atnEntry = entry;
  g.call(from, cl.primary, entry);
  for (unsigned i = 0; i < cl.opCount; ++i) {
    const std::uint32_t afterOp = g.add(basic, rule);
    const std::uint32_t before = g.add(basic, rule);
    g.eps(entry, before);
    const auto& select = b.cfg.nodes[cl.selectNode];
    g.build(b.cfg.childIds[select.sym + i], before, afterOp, rule);
    g.call(afterOp, cl.primary, entry);
  }
  g.eps(entry, to);
}

void build_atn(Built& b) {
  b.returnNode.clear();
  AtnGen g{b};
  // The state the graph stands in once each position is read, what a caret
  // seeds its closure from: a call stands after its own return state, where
  // a caret walks up to name the member a token inside the rule fills.
  b.nodeAfter.assign(b.cfg.nnodes, 0xFFFFFFFFu);
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id)
    if (b.cfg.nodes[id].kind == LlShape::call) b.nodeAfter[id] = 0;
  b.ruleStart.assign(b.cfg.nrules, 0);
  std::vector<std::uint32_t> ruleStop(b.cfg.nrules, 0);
  // A rule the entry never reaches has no states: its start reads as the
  // first state, like the document's own, and no closure ever enters it.
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
    if (!b.liveRules[r]) continue;
    b.ruleStart[r] = g.add(LlAtnKind::basic, r);
    ruleStop[r] = g.add(LlAtnKind::ruleStop, r);
  }
  // The document, as a caller of the entry: without it a closure reaching the
  // entry's end has nowhere to go, and `$end` would be unreachable.
  const std::uint32_t entry = g.add(LlAtnKind::basic, 0);
  const std::uint32_t afterEntry = g.add(LlAtnKind::basic, 0);
  const std::uint32_t done = g.add(LlAtnKind::ruleStop, 0);
  g.call(entry, b.cfg.entryRule, afterEntry);
  g.atom(afterEntry, kEofSymbol, done);
  b.atnEntry = entry;
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
    if (!b.liveRules[r]) continue;
    const LlRuleInfo& u = b.cfg.rules[r];
    if (u.climb != kNoRow)
      build_climb(g, r, b.cfg.climbs[u.climb], b.ruleStart[r], ruleStop[r]);
    else if (u.body != kNoRow)
      g.build(u.body, b.ruleStart[r], ruleStop[r], r);
    else
      g.eps(b.ruleStart[r], ruleStop[r]);
  }
  // The edges go to one pool, each state keeping where its run begins.
  b.nedges = 0;
  b.edgeNode.clear();
  for (std::uint32_t s = 0; s < b.nstates; ++s) {
    b.states[s].edge = b.nedges;
    b.states[s].nedges = static_cast<std::uint32_t>(g.out[s].size());
    for (std::size_t k = 0; k < g.out[s].size(); ++k) {
      b.edges[b.nedges++] = g.out[s][k];
      b.edgeNode.push_back(g.owner[s][k]);
    }
  }
  // The states each rule's calls return to: where a closure that reaches the
  // end of a rule continues when its caller is unknown.
  std::vector<std::vector<std::uint32_t>> back(b.cfg.nrules);
  for (std::uint32_t e = 0; e < b.nedges; ++e)
    if (b.edges[e].kind == LlEdgeKind::rule)
      back[b.edges[e].label].push_back(b.edges[e].follow);
  b.callerBase.assign(b.cfg.nrules + 1, 0);
  for (std::uint16_t r = 0; r < b.cfg.nrules; ++r)
    b.callerBase[r + 1] =
        b.callerBase[r] + static_cast<std::uint32_t>(back[r].size());
  b.callerAt.clear();
  for (std::uint16_t r = 0; r < b.cfg.nrules; ++r)
    b.callerAt.insert(b.callerAt.end(), back[r].begin(), back[r].end());
  b.returnClass.resize(b.nstates);
  for (std::uint32_t s = 0; s < b.nstates; ++s) b.returnClass[s] = s;
  std::map<std::vector<std::uint32_t>, std::uint32_t> byStops;
  std::vector<std::uint8_t> seen(b.nstates, 0), returns(b.nstates, 0);
  for (const std::uint32_t r : b.callerAt) returns[r] = 1;
  std::vector<std::uint32_t> stops, visited, todo;
  for (std::uint32_t s = 0; s < b.nstates; ++s) {
    if (returns[s] == 0) continue;
    stops.clear();
    visited.clear();
    todo.assign(1, s);
    while (!todo.empty()) {
      const std::uint32_t t = todo.back();
      todo.pop_back();
      if (seen[t] != 0) continue;
      seen[t] = 1;
      visited.push_back(t);
      const LlAtnState& st = b.states[t];
      bool stop = st.kind == LlAtnKind::ruleStop;
      for (std::uint32_t k = 0; k < st.nedges; ++k) {
        const LlAtnEdge& e = b.edges[st.edge + k];
        if (e.kind == LlEdgeKind::epsilon)
          todo.push_back(e.target);
        else
          stop = true;
      }
      if (stop) stops.push_back(t);
    }
    for (const std::uint32_t t : visited) seen[t] = 0;
    std::ranges::sort(stops);
    b.returnClass[s] = byStops.try_emplace(stops, s).first->second;
    if (stops.size() == 1 && b.states[stops[0]].kind == LlAtnKind::ruleStop)
      b.returnClass[s] |= kLlTailReturn;
  }
}

// A set of configurations, canonical: the same readings met twice are one
// state of the automaton, whatever order they were reached in.
std::vector<LlConfig> set_key(const std::vector<LlConfig>& cs) {
  std::vector<LlConfig> k = cs;
  std::ranges::sort(k);
  k.erase(std::ranges::unique(k).begin(), k.end());
  return k;
}

// Every token the readings of one set could read next.
DynSet viable_terms(const LlAtnView& atn, const std::vector<LlConfig>& cs) {
  DynSet t;
  for (const LlConfig& c : cs)
    for (const LlAtnEdge& e : atn.outgoing(c.state))
      if (e.kind == LlEdgeKind::atom) t.set(e.label);
  return t;
}

// How many states one choice may explore before it is left to the parse. A
// state that names one arm is never explored, so the last one may still add one
// state per terminal.
constexpr std::size_t kExploreCap = 512;

LlAtnView view_of(const Built& b) {
  return LlAtnView{.states = b.states.data(),
                   .nstates = b.nstates,
                   .edges = b.edges.data(),
                   .ruleStart = b.ruleStart.data(),
                   .callerBase = b.callerBase.data(),
                   .callerAt = b.callerAt.data(),
                   .returnClass = b.returnClass.data()};
}

// True where the exploration closed: the automaton is then a table, and no
// parse ever runs a closure for this choice.
bool explore_decision(Built& b, LlDecision& dec, const LlBuildOptions& opt) {
  const LlAtnView atn = view_of(b);
  LlSimulation sim(atn, 4096);
  std::map<std::vector<LlConfig>, std::uint32_t> index;
  std::vector<std::vector<LlConfig>> setOf;
  std::vector<std::vector<LlDfaEdge>> out;
  std::vector<std::uint8_t> depthOf, apart;
  std::vector<std::uint16_t> nodeOf, armOf;
  // The readings of a set cross the gap before their next token under one
  // configuration, or the set is the parse's to read.
  const bool regions = b.cfg.cfgTriviaAt.size() > 2;
  const auto read_node = [&](const std::vector<LlConfig>& cs) {
    std::uint16_t node = kNoRow;
    apart.push_back(0);
    if (!regions) return node;
    for (const LlConfig& c : cs) {
      const LlAtnState& q = atn.state(c.state);
      for (std::uint32_t i = 0; i < q.nedges; ++i) {
        if (atn.edge(q.edge + i).kind != LlEdgeKind::atom) continue;
        const std::uint16_t at = b.edgeNode[q.edge + i];
        if (at == kNoRow) continue;
        if (node == kNoRow)
          node = at;
        else
          apart.back() |= b.gapCfg[at] != b.gapCfg[node];
      }
    }
    return node;
  };

  const auto intern = [&](std::uint8_t depth) {
    const auto& cs = sim.configs();
    const auto [at, fresh] = index.try_emplace(
        set_key(cs), static_cast<std::uint32_t>(setOf.size()));
    const auto id = at->second;
    if (!fresh) return id;
    setOf.push_back(cs);
    armOf.push_back(sim.decided());
    nodeOf.push_back(read_node(cs));
    out.emplace_back();
    depthOf.push_back(depth);
    return id;
  };

  sim.start(dec.state, kLlCtxNone);
  // The unknown caller also includes nullable returns. A condition reached
  // before reading a token needs the actual input and cannot enter this DFA.
  if (sim.capped() || sim.needs_filter()) return false;
  intern(0);
  for (std::uint32_t at = 0; at < setOf.size(); ++at) {
    // A set naming one arm is an answer; one naming none is a reading no arm
    // survives, which the parse reports rather than predicts.
    if (setOf[at].empty() || armOf[at] != kNoRow) continue;
    if (apart[at]) return false;
    if (depthOf[at] >= opt.depth || setOf.size() >= kExploreCap) return false;
    const DynSet terms = viable_terms(atn, setOf[at]);
    for (const unsigned term : terms.members()) {
      const auto t = static_cast<std::uint16_t>(term);
      sim.reset();
      sim.load(setOf[at]);
      const bool survives = sim.step(t);
      // A pending guard must not remove an arm during token-only exploration.
      // Once one arm remains, its guards belong to parsing that selected arm;
      // the DFA stops at this state without traversing them.
      if (sim.capped() || (sim.needs_filter() && sim.decided() == kNoRow))
        return false;
      const auto to = survives
                          ? intern(static_cast<std::uint8_t>(depthOf[at] + 1))
                          : kLlDfaHandOver;
      out[at].push_back(LlDfaEdge{.term = t, .target = to});
    }
  }
  // A set from which no token decides an arm ends in the parse's own choice
  // however far it reads: the walk hands over before entering it, or a run of
  // such readings costs every token after it once per choice it reaches.
  std::vector<std::uint8_t> decides(setOf.size(), 0);
  for (std::size_t i = 0; i < setOf.size(); ++i)
    decides[i] = armOf[i] != kNoRow;
  for (bool grew = true; grew;) {
    grew = false;
    for (std::size_t i = 0; i < setOf.size(); ++i)
      if (decides[i] == 0 &&
          std::ranges::any_of(out[i], [&](const LlDfaEdge& e) {
            return e.target != kLlDfaHandOver && decides[e.target] != 0;
          }))
        decides[i] = grew = true;
  }
  // Such edges hand over but keep their terminal: the walk lexes under all
  // the readings admit, or a longer spelling reads as a shorter one.
  for (auto& edges : out)
    for (LlDfaEdge& e : edges)
      if (e.target != kLlDfaHandOver && decides[e.target] == 0)
        e.target = kLlDfaHandOver;
  // Into the pool; a state's arm is the one its readings agree on.
  const auto base = static_cast<std::uint32_t>(b.dfa.size());
  bool open = false;
  for (std::size_t i = 0; i < setOf.size(); ++i) {
    const std::uint16_t alt = armOf[i];
    open = open || (alt == kNoRow && std::ranges::all_of(
                                         out[i], [](const LlDfaEdge& e) {
                                           return e.target == kLlDfaHandOver;
                                         }));
    LlDfaState st{.alt = alt,
                  .edge = static_cast<std::uint32_t>(b.dfaEdge.size()),
                  .nedges = static_cast<std::uint32_t>(out[i].size()),
                  .node = nodeOf[i]};
    for (LlDfaEdge e : out[i]) {
      if (e.target != kLlDfaHandOver) e.target += base;
      b.dfaEdge.push_back(e);
    }
    b.dfa.push_back(st);
  }
  dec.dfa = base;
  b.nopen += open;
  return true;
}

// What a position costs to invent: one token per terminal, and for a call the
// cheapest reading of the rule it names. A least fixed point, so a rule that
// reaches itself is not cheaper than the reading that does not.
constexpr std::uint32_t kLlCostless = 0xFFFFFFFFu;

// What an arm costs to enter, not to write whole: once a token is invented the
// construct is open, and what closes it is due only after its text is read.
// `whole` gives the other cost.
struct LlEntry {
  std::uint32_t cost = kLlCostless;
  bool stop = false;
};

Descent<LlEntry> entry_cost(const Built& b, std::uint16_t id,
                            const std::vector<std::uint32_t>& rule, bool opened,
                            bool whole = false) {
  switch (const LlNode& n = b.cfg.nodes[id]; n.kind) {
    using enum LlShape;
    case term:
      co_return {1, false};
    case empty:
    case implied:
      co_return {0, false};
    // A repair never invents the end of input: asserting that the text ends
    // where it does not would silently accept what the assertion refuses.
    case endMark:
    case condition:
      co_return {kLlCostless, false};
    case call:
      co_return {rule[n.sym], false};
    case seq: {
      std::uint32_t sum = 0;
      for (unsigned i = 0; i < n.count; ++i) {
        const LlEntry k = co_await entry_cost(b, b.cfg.childIds[n.sym + i],
                                              rule, opened, whole);
        if (k.cost == kLlCostless) co_return {kLlCostless, false};
        // A rule that calls others twice over doubles what it costs: past the
        // highest finite cost it stays there, dearer than any arm that fits.
        sum = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            std::uint64_t{sum} + k.cost, kLlCostless - 1u));
        if (k.stop) co_return {sum, true};
        if (k.cost != 0) opened = true;
      }
      co_return {sum, false};
    }
    case alt: {
      LlEntry best;
      for (unsigned i = 0; i < n.count; ++i) {
        const LlEntry k = co_await entry_cost(b, b.cfg.childIds[n.sym + i],
                                              rule, opened, whole);
        if (k.cost < best.cost) best = k;
      }
      co_return best;
    }
    case rep: {
      if (n.min == 0) co_return {0, opened && !whole};
      const LlEntry k = co_await entry_cost(b, n.sym, rule, opened, whole);
      if (k.cost == kLlCostless) co_return {kLlCostless, false};
      co_return k;
    }
  }
  co_return {kLlCostless, false};
}

void compute_repair(Built& b, std::uint64_t& work) {
  std::vector rule(b.cfg.nrules, kLlCostless);
  std::vector<std::vector<std::uint16_t>> users(b.cfg.nrules);
  Pending pending(b.cfg.nrules);
  std::vector<std::uint16_t> nodes;
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
    pending.push(r);
    const LlRuleInfo& u = b.cfg.rules[r];
    if (u.climb != kNoRow) {
      users[b.cfg.climbs[u.climb].primary].push_back(r);
    } else if (u.body != kNoRow) {
      nodes.assign(1, u.body);
      while (!nodes.empty()) {
        const auto id = nodes.back();
        nodes.pop_back();
        const LlNode& n = b.cfg.nodes[id];
        if (n.kind == LlShape::call)
          users[n.sym].push_back(r);
        else {
          const auto children = b.cfg.children(n);
          nodes.insert(nodes.end(), children.begin(), children.end());
        }
      }
    }
  }
  while (!pending.empty()) {
    const auto r = static_cast<std::uint16_t>(pending.pop());
    ++work;
    const LlRuleInfo& u = b.cfg.rules[r];
    const std::uint32_t k =
        u.climb != kNoRow ? rule[b.cfg.climbs[u.climb].primary]
        : u.body != kNoRow
            ? descend(entry_cost(b, u.body, rule, false, true)).cost
            : kLlCostless;
    if (k < rule[r]) {
      rule[r] = k;
      for (const auto user : users[r]) pending.push(user);
    }
  }
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    LlDecision& dec = b.cfg.decisions[d];
    const LlNode& n = b.cfg.nodes[dec.node];
    if (n.kind != LlShape::alt) continue;
    std::uint32_t best = kLlCostless;
    for (std::uint16_t i = 0; i < n.count; ++i) {
      const std::uint32_t k =
          descend(entry_cost(b, b.cfg.childIds[n.sym + i], rule, false)).cost;
      if (k < best) {
        best = k;
        dec.repair = i;
      }
    }
    dec.repairCost = best > kLlRepairCostless
                         ? kLlRepairCostless
                         : static_cast<std::uint16_t>(best);
  }
}

// The positions an invocation of a rule can read first, before it has read
// anything: only there does the rule's caller state what the token fills.
void compute_head_nodes(Built& b) {
  b.nodeHead.assign(b.cfg.nnodes, 0);
  RoundMarks seen(b.nstates);
  std::vector<std::uint32_t> todo;
  const auto walk = [&](std::uint16_t rule, auto&& visit) {
    seen.next();
    todo.assign(1, b.ruleStart[rule]);
    seen.first(todo.back());
    while (!todo.empty()) {
      const std::uint32_t at = todo.back();
      todo.pop_back();
      visit(at);
      const LlAtnState& q = b.states[at];
      for (std::uint32_t i = 0; i < q.nedges; ++i) {
        const LlAtnEdge& e = b.edges[q.edge + i];
        if (e.kind == LlEdgeKind::atom ||
            (e.kind == LlEdgeKind::rule && !b.nullable[b.cfg.nnodes + e.label]))
          continue;
        const std::uint32_t next =
            e.kind == LlEdgeKind::rule ? e.follow : e.target;
        if (seen.first(next)) todo.push_back(next);
      }
    }
  };
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r)
    walk(r, [&](std::uint32_t at) {
      const LlAtnState& q = b.states[at];
      for (std::uint32_t i = 0; i < q.nedges; ++i) {
        const std::uint16_t node = b.edgeNode[q.edge + i];
        if ((b.edges[q.edge + i].kind == LlEdgeKind::atom ||
             b.edges[q.edge + i].kind == LlEdgeKind::rule) &&
            node != kNoRow && b.cfg.nodes[node].kind != LlShape::endMark)
          b.nodeHead[node] = 1;
      }
    });
  // An infix operator fills its own member, even when its primary is empty.
  for (std::uint16_t i = 0; i < b.cfg.nclimbOps; ++i) {
    const auto leaf = b.cfg.atomic_operator(b.cfg.climbOps[i].body);
    if (leaf != kNoRow) b.nodeHead[leaf] = 0;
  }
}

void compute_cuts(Built& b) {
  b.cut.assign(b.cfg.ngapConds, DynSet{});
  for (unsigned k = 0; k < b.cfg.ngapConds; ++k)
    gap_cut_alone(b.cfg, k, &b.cut[k]);
}

// What a commit's exit refuses where it follows a loop, by the loop's
// decision: the exit stands right behind the loop in its sequence.
std::vector<std::uint16_t> loop_exits(const Built& b) {
  std::vector exits(b.cfg.ndecisions, kNoRow);
  const auto last_loop = [&](std::uint16_t id) {
    for (const LlNode* n = &b.cfg.nodes[id]; n->kind == LlShape::seq;
         n = &b.cfg.nodes[id]) {
      if (n->count == 0) break;
      id = b.cfg.childIds[n->sym + n->count - 1u];
    }
    return id;
  };
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    if (n.kind != LlShape::seq) continue;
    const auto k = b.cfg.children(n);
    for (unsigned i = 0; i + 1 < k.size(); ++i) {
      const LlNode& next = b.cfg.nodes[k[i + 1]];
      if (next.kind != LlShape::condition || b.cut[next.sym].none()) continue;
      const LlNode& loop = b.cfg.nodes[last_loop(k[i])];
      if (loop.kind == LlShape::rep) exits[loop.decision] = next.sym;
    }
  }
  return exits;
}

// The terminals a reading takes from `from` before it reads anything, over any
// caller and short of the choice `stop`. False where a condition stands on the
// way.
bool reads_short_of(const LlAtnView& atn, std::uint32_t from,
                    std::uint32_t stop, DynSet& out) {
  std::vector<std::uint8_t> seen(atn.nstates, 0);
  std::vector todo{from};
  while (!todo.empty()) {
    const std::uint32_t s = todo.back();
    todo.pop_back();
    if (s == stop || seen[s] != 0) continue;
    seen[s] = 1;
    if (const LlAtnState& q = atn.state(s); q.kind == LlAtnKind::ruleStop)
      todo.insert(todo.end(), atn.callerAt + atn.callerBase[q.rule],
                  atn.callerAt + atn.callerBase[q.rule + 1]);
    for (const LlAtnEdge& e : atn.outgoing(s)) {
      if (e.kind == LlEdgeKind::condition) return false;
      if (e.kind == LlEdgeKind::atom)
        out.set(e.label);
      else
        todo.push_back(e.target);
    }
  }
  return true;
}

// A loop exit that reads a token only through this same choice in a caller,
// such as a dangling `else`, reads nothing a turn does not: the first arm wins,
// so the token goes to the turn.
void give_the_turn_what_it_dominates(Built& b, const LlAtnView& atn,
                                     const LlDecision& dec) {
  if (const LlNode& n = b.cfg.nodes[dec.node];
      n.kind != LlShape::rep || b.nullable[n.sym])
    return;
  DynSet& out = b.laSet[dec.set + 1];
  DynSet both = out;
  both &= b.laSet[dec.set];
  if (both.none()) return;
  DynSet read;
  if (const std::uint32_t target =
          atn.edge(atn.state(dec.state).edge + 1u).target;
      !reads_short_of(atn, target, dec.state, read))
    return;
  if (!read.intersects(both)) out.andnot(both);
}

// Decides a choice by one token when that separates its arms (a switch);
// otherwise the parse runs a closure for it.
void compute_decisions(Built& b, const LlBuildOptions& opt) {
  b.laSet.clear();
  const LlAtnView atn = view_of(b);
  LlSimulation prefix(atn, 4096);
  const std::vector<std::uint16_t> exits = loop_exits(b);
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    LlDecision& dec = b.cfg.decisions[d];
    const LlNode& n = b.cfg.nodes[dec.node];
    const DynSet& after = b.after[dec.node];
    const auto base = static_cast<std::uint32_t>(b.laSet.size());
    if (n.kind == LlShape::rep) {
      // Entering the body wins a turn; leaving it is what follows the loop.
      b.laSet.push_back(b.lookahead(n.sym, after));
      b.laSet.push_back(after);
      if (exits[d] != kNoRow) b.laSet.back().andnot(b.cut[exits[d]]);
    } else {
      const auto k = b.cfg.children(n);
      for (const auto child : k) {
        b.laSet.push_back(b.lookahead(child, after));
        const LlNode& arm = b.cfg.nodes[child];
        if (arm.kind == LlShape::condition)
          b.laSet.back().andnot(b.cut[arm.sym]);
      }
    }
    dec.set = base;
    // A choice the entry never reaches is never asked, and has no graph.
    if (!b.liveRules[dec.rule]) continue;
    if (opt.staticPredict) give_the_turn_what_it_dominates(b, atn, dec);
    // No arm reads a token of an arm before it.
    bool disjoint = opt.staticPredict;
    const unsigned nalts = dec.nalts;
    DynSet read;
    for (unsigned i = 0; disjoint && i < nalts; ++i)
      disjoint = !read.intersects(b.laSet[base + i]) &&
                 (read |= b.laSet[base + i], true);
    if (disjoint && b.cfg.ngapConds != 0) {
      // A guard elsewhere does not stop this token from selecting an arm once
      // the initial closure has no pending condition through any caller. A
      // bounded or conditional closure keeps the adaptive path.
      prefix.forget();
      prefix.start(dec.state, kLlCtxNone);
      disjoint =
          !prefix.capped() &&
          !prefix.needs_filter([&](std::uint16_t condition) {
            return condition < b.cfg.ngapConds && b.cut[condition].none();
          });
    }
    if (disjoint) {
      dec.how = LlPredict::single;
      continue;
    }
    // What a ceiling of tokens tells apart is a table; the rest is a closure
    // the parse runs, over the stack it actually stands on.
    dec.how = opt.staticPredict && explore_decision(b, dec, opt)
                  ? LlPredict::staged
                  : LlPredict::adaptive;
    if (dec.how == LlPredict::adaptive) ++b.nadaptive;
  }
}

// Joins two runs into one interned chain and keeps the pair that made it, which
// an entry is written from. A pair asked again reads its chain back.
class ChainJoin {
 public:
  ChainJoin(Built& b, std::uint64_t& reads) : b(b), _reads(reads) {}

  std::uint16_t operator()(std::uint16_t held, std::uint16_t over) {
    const ChainBuild left =
        held == kNoRow ? ChainBuild{} : b.writes.chainBuild[held];
    const ChainBuild right =
        over == kNoRow ? ChainBuild{} : b.writes.chainBuild[over];
    const std::uint16_t r = chain_of(held, over, right);
    if (r == kNoRow) return r;
    // Joined chains still carry their creations: the interned chain starts with
    // an empty build row, which a later join would read as one that makes
    // nothing.
    b.writes.chainBuild[r] = {.makes = left.makes || right.makes,
                              .creates = left.creates || right.creates,
                              .nest = left.nest || right.nest,
                              .doc = b.writes.chainBuild[r].doc};
    if (r >= b.origin.size())
      b.origin.resize(b.writes.nchains, LlFuse{kNoRow, kNoRow});
    if (r != held && r != over && b.origin[r].from == kNoRow)
      b.origin[r] = LlFuse{held, over};
    return r;
  }

 private:
  std::uint16_t chain_of(std::uint16_t held, std::uint16_t over,
                         const ChainBuild& right) {
    // A later join may restate what `over` makes, which decides how it joins.
    const std::uint64_t key = std::uint64_t{right.makes && !right.creates} << 32 |
                              std::uint64_t{held} << 16 | over;
    if (const auto at = _made.find(key); at != _made.end()) return at->second;
    ++_reads;
    const std::uint16_t r = intern(held, over, right);
    if (r != kNoRow) _made.emplace(key, r);
    return r;
  }

  std::uint16_t intern(std::uint16_t held, std::uint16_t over,
                       const ChainBuild& right) {
    // Closing the same value twice on one record is idempotent. A recursive
    // adoption (parenthesized expressions, for example) can otherwise append
    // that close forever while computing the possible rows of a rule.
    if (held != kNoRow && over != kNoRow && right.makes && !right.creates) {
      const Chain x = b.writes.chains[held], y = b.writes.chains[over];
      const auto* acts = b.writes.acts.data();
      if (x.count != 0 && y.count != 0 &&
          acts[x.off + x.count - 1] == acts[y.off]) {
        std::vector<std::uint16_t> joined(acts + x.off, acts + x.off + x.count);
        joined.insert(joined.end(), acts + y.off + 1, acts + y.off + y.count);
        return b.writes.intern_chain(joined.data(), joined.size(),
                                     b.writes.doc_of(held, over));
      }
    }
    return b.writes.concat(held, over);
  }

  Built& b;
  std::uint64_t& _reads;
  std::unordered_map<std::uint64_t, std::uint16_t> _made;
};

void compute_fusion(Built& b, LlBuildWork& work) {
  b.origin.assign(b.writes.nchains, LlFuse{kNoRow, kNoRow});
  ChainJoin join(b, work.joins);
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    if (!n.owns || n.kind != LlShape::seq || n.count != 1 || n.row == kNoRow ||
        !b.writes.chainBuild[n.row].nest || n.foldStep)
      continue;
    const LlNode& only = b.cfg.nodes[b.cfg.childIds[n.sym]];
    if (only.kind != LlShape::term) continue;
    const std::uint16_t stated =
        only.row == kNoRow ? n.row : join(n.row, only.row);
    b.cfg.nodes[id].leafRow = join(b.cfg.terms[only.sym].element, stated);
  }
  // A terminal's own conversion joins its position's chain first: what a leaf
  // carries is then one row, and every walk below reads it as such.
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    LlNode& n = b.cfg.nodes[id];
    if (n.kind != LlShape::term) continue;
    const std::uint16_t own = b.cfg.terms[n.sym].element;
    n.row = n.row == kNoRow ? own : join(own, n.row);
  }
  LlRecordPlan records(b.cfg, b.writes);
  records.count_rows(&work.rows);
  records.compute_rows(b.nullable, join);
  b.fuseBase.assign(b.cfg.nnodes + 1, 0);
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    b.fuseBase[id] = static_cast<std::uint32_t>(b.fuse.size());
    if (n.kind != LlShape::call || n.row == kNoRow || n.owns) continue;
    for (std::uint16_t x : records.rule_rows(n.sym))
      b.fuse.push_back(LlFuse{x, join(x, n.row)});
  }
  b.fuseBase[b.cfg.nnodes] = static_cast<std::uint32_t>(b.fuse.size());
  // What an owning group takes when the text gives it one position: two
  // records over one span make a node nobody wrote, so two creations do not
  // join and the group creates its own.
  b.ownBase.assign(b.cfg.nnodes + 1, 0);
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    b.ownBase[id] = static_cast<std::uint32_t>(b.fuse.size());
    if (!n.owns || n.row == kNoRow) continue;
    records.own_joins(static_cast<std::uint16_t>(id), join,
                      [&](const LlFuse& f) { b.fuse.push_back(f); });
  }
  b.ownBase[b.cfg.nnodes] = static_cast<std::uint32_t>(b.fuse.size());

  // A climb's three positions state their members over records too: the
  // operand's, whichever reading of it the text took, and, from the second
  // turn on, the node the climb itself just created.
  for (std::uint16_t k = 0; k < b.cfg.nclimbs; ++k) {
    LlClimb& cl = b.cfg.climbs[k];
    std::vector<std::uint16_t> hands = records.rule_rows(cl.primary);
    hands.push_back(cl.node);
    records.keep_first(hands);
    cl.leftBase = static_cast<std::uint32_t>(b.fuse.size());
    for (std::uint16_t h : hands)
      b.fuse.push_back(LlFuse{h, join(h, cl.leftRow)});
    cl.leftCount = static_cast<std::uint32_t>(b.fuse.size()) - cl.leftBase;
    cl.rightBase = static_cast<std::uint32_t>(b.fuse.size());
    for (std::uint16_t h : hands)
      b.fuse.push_back(LlFuse{h, join(h, cl.rightRow)});
    cl.rightCount = static_cast<std::uint32_t>(b.fuse.size()) - cl.rightBase;
  }
}

// The repetitions that take another turn before they return to the caller
// when both read the text: a token of the caller's, longer, does not override
// a continuation of the body.
std::vector<std::uint8_t> repeat_scopes(const Built& b, const LlAtnView& atn,
                                        const std::uint8_t* shared) {
  std::vector<std::uint8_t> out(b.cfg.ndecisions, 0);
  LlReturnPool pool;
  pool.read_returns_by_class(atn.returnClass);
  LlEpisodeSim sim(atn, pool, false);
  sim.share_calls(shared);
  std::vector<LlThread> seeded;
  std::vector<std::pair<std::uint32_t, std::uint16_t>> visited;
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    const LlDecision& dec = b.cfg.decisions[d];
    if (!b.cfg.repeats(dec) || !b.liveRules[dec.rule]) continue;
    sim.reset();
    visited.clear();
    sim.seed(dec.state, kLlCtxNone, seeded, &visited);
    bool local = false, caller = false;
    for (const auto& [at, root] : visited) {
      if (root != 1) continue;
      const auto& state = atn.state(at);
      caller = caller ||
               (state.kind == LlAtnKind::ruleStop && state.rule == dec.rule);
      for (const LlAtnEdge& e : atn.outgoing(at))
        if (e.kind == LlEdgeKind::atom) {
          local = local || state.rule == dec.rule;
          caller = caller || state.rule != dec.rule;
        }
    }
    out[d] = !local || caller;
  }
  return out;
}

// What an episode reads besides the graph, computed here so that no parse
// works it out on its first episode. Every table keeps them: an automaton the
// construction closed can still leave a choice to the episode.
void compute_episode_flags(Built& b) {
  const LlAtnView atn = view_of(b);
  const auto none = [](const std::vector<std::uint8_t>& flags) {
    return std::ranges::none_of(flags, [](std::uint8_t f) { return f != 0; });
  };
  std::vector<std::uint8_t> shared = ll_shared_rules(atn, b.cfg.nrules);
  b.repeatScopes = repeat_scopes(b, atn, shared.data());
  if (none(b.repeatScopes)) b.repeatScopes.clear();
  if (!none(shared)) b.sharedRules = std::move(shared);
}

}  // namespace

std::vector<std::uint8_t> ll_shared_rules(const LlAtnView& atn,
                                          std::uint16_t nrules,
                                          std::uint64_t* visits) {
  std::vector<std::uint8_t> nullable(nrules, 0), shared(nrules, 0);
  // Only a state with several ways out shares a rule: with none, the regions
  // below would be built for nothing.
  bool forks = false;
  for (std::uint32_t at = 0; at < atn.nstates && !forks; ++at)
    forks = atn.state(at).nedges >= 2;
  if (!forks) return shared;
  std::vector<std::uint32_t> seen(atn.nstates, 0), todo;
  // Each rule walks its own states once. A call waits until its rule can
  // return unread; conditions, like tokens, stop this unguarded walk.
  {
    std::vector<std::vector<std::uint32_t>> waiting(nrules);
    for (std::uint16_t r = 0; r < nrules; ++r) todo.push_back(atn.ruleStart[r]);
    while (!todo.empty()) {
      const auto at = todo.back();
      todo.pop_back();
      if (seen[at] != 0) continue;
      seen[at] = 1;
      if (const auto& state = atn.state(at);
          state.kind == LlAtnKind::ruleStop && nullable[state.rule] == 0) {
        nullable[state.rule] = 1;
        const auto& calls = waiting[state.rule];
        todo.insert(todo.end(), calls.begin(), calls.end());
      }
      for (const LlAtnEdge& e : atn.outgoing(at)) {
        if (e.kind == LlEdgeKind::epsilon)
          todo.push_back(e.target);
        else if (e.kind == LlEdgeKind::rule) {
          if (nullable[e.label] != 0)
            todo.push_back(e.follow);
          else
            waiting[e.label].push_back(e.follow);
        }
      }
    }
  }
  RoundMarks scanned(atn.nstates);
  // The rules called from `from` before a token is read, told to `call`.
  const auto scan = [&](std::uint32_t from, auto&& call) {
    scanned.next();
    todo.assign(1, from);
    while (!todo.empty()) {
      const std::uint32_t at = todo.back();
      todo.pop_back();
      if (!scanned.first(at)) continue;
      for (const LlAtnEdge& e : atn.outgoing(at)) {
        if (e.kind == LlEdgeKind::epsilon) {
          todo.push_back(e.target);
        } else if (e.kind == LlEdgeKind::rule) {
          call(e.label);
          if (nullable[e.label] != 0) todo.push_back(e.follow);
        }
      }
    }
  };
  // A rule the entry never reaches has no states, its start being another's,
  // and no edge calls it.
  std::vector<std::vector<std::uint32_t>> heads(nrules);
  for (std::uint16_t r = 0; r < nrules; ++r) {
    if (atn.state(atn.ruleStart[r]).rule != r) continue;
    auto& row = heads[r];
    scan(atn.ruleStart[r], [&](std::uint16_t called) { row.push_back(called); });
    std::ranges::sort(row);
    row.erase(std::ranges::unique(row).begin(), row.end());
  }
  // Components close callees first, so a rule and all it calls stand between
  // its component and `lowest` of it. Two ways whose spans do not overlap
  // share nothing, and neither is walked.
  const auto scc = StrongComponents<>::of(heads);
  std::vector<std::uint32_t> byComponent(nrules), lowest(nrules);
  std::iota(byComponent.begin(), byComponent.end(), 0u);
  std::ranges::sort(byComponent, {},
                    [&](std::uint32_t r) { return scc.component[r]; });
  std::iota(lowest.begin(), lowest.end(), 0u);
  for (const std::uint32_t r : byComponent)
    for (const std::uint32_t called : heads[r])
      lowest[scc.component[r]] = std::min(lowest[scc.component[r]],
                                          lowest[scc.component[called]]);
  // A rule reached by a second way out of a fork is shared, with all it calls:
  // the first way walked them already. A walk stops at what is shared, so it
  // keeps one mark per rule.
  constexpr std::uint32_t kNoFork = 0xffffffffu;
  struct Span {
    std::uint32_t low, high, way;
  };
  std::vector<std::uint32_t> forkOf(nrules, kNoFork), wayOf(nrules, 0);
  std::vector<std::uint8_t> twice(nrules, 0), overlaps;
  std::vector<std::pair<std::uint32_t, std::uint16_t>> calls;
  std::vector<std::uint32_t> stack;
  std::vector<Span> spans;
  for (std::uint32_t fork = 0; fork < atn.nstates; ++fork) {
    if (atn.state(fork).nedges < 2) continue;
    calls.clear();
    spans.clear();
    std::uint32_t way = 0;
    for (const LlAtnEdge& e : atn.outgoing(fork)) {
      const std::size_t before = calls.size();
      const auto call = [&](std::uint16_t rule) {
        calls.emplace_back(way, rule);
      };
      if (e.kind == LlEdgeKind::epsilon) {
        scan(e.target, call);
      } else if (e.kind == LlEdgeKind::rule) {
        call(e.label);
        if (nullable[e.label] != 0) scan(e.follow, call);
      }
      if (calls.size() != before) {
        Span span{0xffffffffu, 0, way};
        for (std::size_t i = before; i < calls.size(); ++i) {
          const std::uint32_t component = scc.component[calls[i].second];
          span.low = std::min(span.low, lowest[component]);
          span.high = std::max(span.high, component);
        }
        spans.push_back(span);
      }
      ++way;
    }
    if (spans.size() < 2) continue;
    // Sorted by where they begin, a span overlaps another if it begins inside
    // one before it, or if the next begins inside it.
    std::ranges::sort(spans, {}, &Span::low);
    overlaps.assign(way, 0);
    std::uint32_t reach = spans[0].high;
    for (std::size_t i = 1; i < spans.size(); ++i) {
      if (spans[i].low <= reach) overlaps[spans[i].way] = 1;
      if (spans[i].low <= spans[i - 1].high) overlaps[spans[i - 1].way] = 1;
      reach = std::max(reach, spans[i].high);
    }
    for (const auto& [taken, first] : calls) {
      if (overlaps[taken] == 0) continue;
      stack.assign(1, first);
      while (!stack.empty()) {
        const std::uint32_t rule = stack.back();
        stack.pop_back();
        if (visits != nullptr) ++*visits;
        if (twice[rule] != 0) continue;
        if (forkOf[rule] != fork) {
          forkOf[rule] = fork;
          wayOf[rule] = taken;
        } else if (wayOf[rule] != taken) {
          twice[rule] = 1;
        } else {
          continue;
        }
        stack.insert(stack.end(), heads[rule].begin(), heads[rule].end());
      }
    }
  }
  for (std::uint16_t r = 0; r < nrules; ++r)
    shared[r] = twice[r] != 0 && nullable[r] == 0 ? 1 : 0;
  return shared;
}

LlBuiltProgram ll_build_program(const LlLowered& low, LlBuildOptions opt) {
  Built b;
  b.cfg = low.cfg;
  b.writes = low.writes;
  b.fault = low.fault;
  // A pass nobody counts writes to `uncounted`.
  LlBuildWork uncounted;
  LlBuildWork& work = opt.work ? *opt.work : uncounted;
  b.liveRules = reached_from(b.cfg, b.cfg.entryRule);
  refuse_skipped_terminal(b);
  const ShapeGraph shapes(b.cfg);
  b.nullable = compute_nullable(b.cfg, shapes, work.nullable);
  // EOF constrains FIRST but still makes no progress through the input.
  const auto empty = compute_nullable(b.cfg, shapes, uncounted.nullable, true);
  // A refused grammar skips the walks below: a counted repetition of a body
  // that reads nothing nests as deep as its bound, and the refusal must be
  // reported before any walk follows that depth.
  if (b.fault) return b;
  compute_first(b, shapes, work.first);
  refuse_left_recursion(b, empty);
  refuse_unproductive(b, shapes, work.productive);
  // Nor does one refused here: a left-cyclic rule has no fixpoint for the walks
  // below, and the follow pass would descend it for ever.
  if (b.fault) return b;
  compute_after(b.cfg, b.nullable, b.first, b.after, b.ruleFollow, work.follow);
  compute_cuts(b);
  compute_lead(b, shapes, uncounted.first);
  build_atn(b);
  LexAlphabet alphabet;
  alphabet.terminals(b, [](LlLowCfg& g, std::string_view text) {
    return g.intern_name(text);
  });
  if (b.cfg.lowerFail()) return b;
  compute_gaps(b, shapes, alphabet, work);
  compute_decisions(b, opt);
  refuse_section_lookahead(b);
  compute_repair(b, work.repair);
  compute_fusion(b, work);
  if (b.writes.capped) b.cap_hit(kTooManyChains);
  ll_build_lexer(b, alphabet, &work.overtakes);
  if (b.cfg.lowerFail()) return b;
  compute_head_nodes(b);
  compute_episode_flags(b);
  return b;
}

}  // namespace cursiv::engine
