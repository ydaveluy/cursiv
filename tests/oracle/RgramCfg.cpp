#include "oracle/RgramCfg.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cursiv {
namespace {

using Sym = RgramCfg::Sym;

Sym term(char t) { return {.kind = Sym::Kind::term, .tok = t}; }
Sym call(unsigned nt) { return {.kind = Sym::Kind::nt, .nt = nt}; }

// A condition inside a region reads the region's own bytes: past its last
// token, a blank and not the next token.
bool holds(const Sym& s, const RgramDocument& d, unsigned i) {
  const auto n = static_cast<unsigned>(d.toks.size());
  const bool blank = s.tight && !d.gaps[i].empty();
  const char here = i < n && !blank ? d.toks[i] : '\0';
  static const std::string kEmpty;
  const std::string& gap = s.tight ? kEmpty : d.gaps[i];
  const bool line = gap.find('\n') != std::string::npos;
  switch (s.cond) {
    case RgramCond::notTok:
      return here != s.tok;
    case RgramCond::andTok:
      return here == s.tok;
    case RgramCond::line:
      return line;
    case RgramCond::noLine:
      return !line;
    case RgramCond::lineOrTok:
      return line || here == s.tok;
    case RgramCond::none:
      return gap.empty();
    case RgramCond::notNone:
      return !gap.empty();
    case RgramCond::lineOrEof:
      return line || i == n;
    case RgramCond::eof:
      return i == n;
  }
  return false;
}

}  // namespace

RgramCfg::RgramCfg(const RgramGrammar& g) : _rules(&g.rules) {
  const auto k = static_cast<unsigned>(g.rules.size());
  _tightOf.assign(k, 0);
  _tokOf.assign(256, 0);
  for (unsigned i = 0; i < k; ++i) fresh("N" + std::to_string(i));
  for (unsigned i = 0; i < k; ++i) add(i, g.rules[i], false);
}

unsigned RgramCfg::fresh(std::string name, bool isRegion) {
  of.emplace_back();
  node.push_back(std::move(name));
  region.push_back(isRegion ? 1 : 0);
  return static_cast<unsigned>(of.size() - 1);
}

void RgramCfg::push(unsigned lhs, std::vector<Sym> rhs) {
  of[lhs].push_back(static_cast<unsigned>(prods.size()));
  prods.push_back({lhs, std::move(rhs)});
}

void RgramCfg::add(unsigned lhs, const RgramExpr& e, bool tight) {
  if (e.kind == RgramKind::alt) {
    for (const RgramExpr& x : e.kids) push(lhs, syms(x, tight));
  } else {
    push(lhs, syms(e, tight));
  }
}

std::vector<Sym> RgramCfg::syms(const RgramExpr& e, bool tight) {
  switch (e.kind) {
    case RgramKind::seq: {
      std::vector<Sym> out;
      for (const RgramExpr& x : e.kids) {
        std::vector<Sym> part = syms(x, tight);
        out.insert(out.end(), part.begin(), part.end());
      }
      return out;
    }
    case RgramKind::skip: {
      const unsigned x = fresh({}, true);
      push(x, syms(e.kids[0], true));
      return {call(x)};
    }
    case RgramKind::ref:
      if (!tight) return {call(e.ref)};
      // A rule a region reaches is read a second time, tight.
      if (_tightOf[e.ref] == 0) {
        const unsigned c = fresh("N" + std::to_string(e.ref));
        _tightOf[e.ref] = c + 1;
        add(c, (*_rules)[e.ref], true);
      }
      return {call(_tightOf[e.ref] - 1)};
    case RgramKind::cond:
      return {Sym{.kind = Sym::Kind::cond,
                  .tok = e.tok,
                  .cond = e.cond,
                  .tight = tight}};
    case RgramKind::tok:
      return {term(e.tok)};
    case RgramKind::tokNode: {
      unsigned& t = _tokOf[static_cast<unsigned char>(e.tok)];
      if (t == 0) {
        t = fresh(std::string("T") + e.tok) + 1;
        push(t - 1, {term(e.tok)});
      }
      return {call(t - 1)};
    }
    default:
      break;
  }
  const unsigned x = fresh();
  if (e.kind == RgramKind::alt) {
    for (const RgramExpr& a : e.kids) push(x, syms(a, tight));
    return {call(x)};
  }
  const std::vector<Sym> body = syms(e.kids[0], tight);
  const Sym stop{.kind = Sym::Kind::cond,
                 .tok = e.committed ? e.kids[0].kids[0].tok : '\0',
                 .cond = RgramCond::notTok,
                 .tight = tight};
  const auto self_then_body = [&](unsigned self) {
    std::vector<Sym> out{call(self)};
    out.insert(out.end(), body.begin(), body.end());
    return out;
  };
  switch (e.kind) {
    case RgramKind::opt:
      push(x, e.committed ? std::vector<Sym>{stop} : std::vector<Sym>{});
      push(x, body);
      break;
    case RgramKind::many:
      push(x, {});
      push(x, self_then_body(x));
      break;
    case RgramKind::some:
      push(x, body);
      push(x, self_then_body(x));
      break;
    case RgramKind::sep: {
      const unsigned y = fresh();
      push(y, body);
      std::vector<Sym> more{call(y), term(',')};
      more.insert(more.end(), body.begin(), body.end());
      push(y, std::move(more));
      push(x, {});
      push(x, {call(y)});
      break;
    }
    case RgramKind::rep:
      for (unsigned n = e.lo; n <= e.hi; ++n) {
        std::vector<Sym> turns;
        for (unsigned t = 0; t < n; ++t)
          turns.insert(turns.end(), body.begin(), body.end());
        push(x, std::move(turns));
      }
      break;
    default:
      break;
  }
  if (e.committed && (e.kind == RgramKind::many || e.kind == RgramKind::some))
    return {call(x), stop};
  return {call(x)};
}

// `glued` marks a token inside a region past its first: the gap before it
// is the region's, and a drawn document mostly leaves it empty.
bool RgramCfg::derive(RgramRandom& r, unsigned nt, unsigned depth,
                      bool inRegion, std::vector<char>& toks,
                      std::vector<char>& glued) const {
  if (toks.size() > 14 || depth > 256) return false;
  const std::vector<unsigned>& ps = of[nt];
  unsigned k = ps[0];
  if (depth > 7) {
    for (const unsigned q : ps)
      if (prods[q].rhs.size() < prods[k].rhs.size()) k = q;
  } else {
    k = ps[r.below(static_cast<unsigned>(ps.size()))];
  }
  const std::size_t first = toks.size();
  const bool inner = inRegion || region[nt] != 0;
  for (const Sym& s : prods[k].rhs) {
    const bool past = inner && (inRegion || toks.size() > first);
    if (s.kind == Sym::Kind::term) {
      glued.push_back(past ? 1 : 0);
      toks.push_back(s.tok);
    } else if (s.kind == Sym::Kind::nt &&
               !derive(r, s.nt, depth + 1, past, toks, glued)) {
      return false;
    }
  }
  return true;
}

std::vector<RgramDocument> RgramCfg::documents(std::uint64_t seed,
                                               unsigned count) const {
  constexpr char kAny[] = {'a', 'b', 'c', 'd', ','};
  const char* const kGaps[] = {"", " ", " ", "\n", "  \n "};
  const char* const kEdges[] = {"", "", "\n"};
  RgramRandom r{.state = seed};
  std::vector<RgramDocument> out;
  std::set<std::string> seen;
  for (unsigned tries = 0; out.size() < count && tries < count * 20; ++tries) {
    std::vector<char> toks, glued;
    if (!derive(r, 0, 0, false, toks, glued)) continue;
    // Some texts are damaged by one token: inserted, dropped or replaced.
    if (r.unit() < 0.4 && !toks.empty()) {
      const unsigned m = r.below(3);
      const unsigned j = r.below(static_cast<unsigned>(toks.size()) + 1);
      if (m == 0) {
        toks.insert(toks.begin() + j, kAny[r.below(5)]);
        glued.insert(glued.begin() + j, 0);
      } else if (m == 1 && j < toks.size()) {
        toks.erase(toks.begin() + j);
        glued.erase(glued.begin() + j);
      } else if (j < toks.size()) {
        toks[j] = kAny[r.below(5)];
      }
    }
    std::vector<std::string> gaps(toks.size() + 1);
    for (std::string& g : gaps) g = kGaps[r.below(5)];
    for (std::size_t q = 0; q < toks.size(); ++q)
      if (glued[q] != 0 && r.unit() < 0.9) gaps[q].clear();
    gaps.front() = kEdges[r.below(3)];
    gaps.back() = kEdges[r.below(3)];
    std::string text = gaps[0];
    for (std::size_t q = 0; q < toks.size(); ++q) {
      text += toks[q];
      text += gaps[q + 1];
    }
    if (!seen.insert(text).second) continue;
    out.push_back({std::move(text), std::move(toks), std::move(gaps)});
  }
  return out;
}

RgramVerdict RgramCfg::read(const RgramDocument& d, bool tree) const {
  struct Item {
    unsigned k, dot, from;
  };
  const auto n = static_cast<unsigned>(d.toks.size());
  std::vector<std::vector<Item>> sets(n + 1);
  std::vector<std::unordered_set<std::uint64_t>> seen(n + 1);
  const auto add = [&](unsigned i, Item it) {
    const std::uint64_t key = (std::uint64_t{it.k} << 40) |
                              (std::uint64_t{it.dot} << 20) | it.from;
    if (!seen[i].insert(key).second) return false;
    sets[i].push_back(it);
    return true;
  };
  // A region's span holds no gap between its own tokens.
  const auto gapped = [&](unsigned from, unsigned to) {
    for (unsigned m = from + 1; m < to; ++m)
      if (!d.gaps[m].empty()) return true;
    return false;
  };
  const auto complete = [&](const Item& it) {
    return it.dot == prods[it.k].rhs.size();
  };
  for (const unsigned k : of[0]) add(0, {k, 0, 0});
  for (unsigned i = 0; i <= n; ++i) {
    for (bool changed = true; changed;) {
      changed = false;
      for (std::size_t q = 0; q < sets[i].size(); ++q) {
        const Item it = sets[i][q];
        const Prod& p = prods[it.k];
        if (complete(it)) {
          if (region[p.lhs] != 0 && gapped(it.from, i)) continue;
          for (std::size_t c = 0; c < sets[it.from].size(); ++c) {
            const Item up = sets[it.from][c];
            const Prod& u = prods[up.k];
            if (up.dot < u.rhs.size() && u.rhs[up.dot].kind == Sym::Kind::nt &&
                u.rhs[up.dot].nt == p.lhs)
              changed = add(i, {up.k, up.dot + 1, up.from}) || changed;
          }
          continue;
        }
        const Sym& s = p.rhs[it.dot];
        if (s.kind == Sym::Kind::nt) {
          for (const unsigned k : of[s.nt]) changed = add(i, {k, 0, i}) || changed;
          // What derives nothing here was completed before this item came.
          for (std::size_t c = 0; c < sets[i].size(); ++c) {
            const Item e = sets[i][c];
            if (e.from == i && prods[e.k].lhs == s.nt && complete(e))
              changed = add(i, {it.k, it.dot + 1, it.from}) || changed;
          }
        } else if (s.kind == Sym::Kind::cond) {
          if (holds(s, d, i))
            changed = add(i, {it.k, it.dot + 1, it.from}) || changed;
        } else if (i < n && d.toks[i] == s.tok) {
          add(i + 1, {it.k, it.dot + 1, it.from});
        }
      }
    }
  }
  RgramVerdict v;
  for (const Item& it : sets[n])
    if (prods[it.k].lhs == 0 && complete(it) && it.from == 0) v.accepted = true;
  if (!tree || !v.accepted) return v;

  // Every tree over a span, up to a few: more, or a cycle, is no answer.
  constexpr std::size_t kCap = 8;
  using Key = std::tuple<unsigned, unsigned, unsigned>;
  using Kids = std::vector<std::string>;
  using Vals = std::set<Kids>;
  std::map<Key, std::vector<unsigned>> done;
  for (unsigned j = 0; j <= n; ++j)
    for (const Item& it : sets[j])
      if (complete(it) &&
          !(region[prods[it.k].lhs] != 0 && gapped(it.from, j)))
        done[{prods[it.k].lhs, it.from, j}].push_back(it.k);
  std::map<Key, Vals> memo;
  std::set<Key> busy;
  bool over = false;
  std::function<Vals(unsigned, unsigned, unsigned, unsigned)> run;
  const std::function<Vals(unsigned, unsigned, unsigned)> span =
      [&](unsigned a, unsigned i, unsigned j) -> Vals {
    const Key key{a, i, j};
    if (const auto m = memo.find(key); m != memo.end()) return m->second;
    if (busy.contains(key)) {
      over = true;
      return {};
    }
    busy.insert(key);
    Vals out;
    if (const auto at = done.find(key); at != done.end())
      for (const unsigned k : at->second)
        for (const Kids& kids : run(k, 0, i, j)) {
          out.insert(kids);
          if (out.size() > kCap) {
            over = true;
            break;
          }
        }
    if (!node[a].empty()) {
      Vals named;
      for (const Kids& kids : out) {
        std::string s = node[a] + "{";
        for (std::size_t q = 0; q < kids.size(); ++q)
          s += "kids" + std::to_string(q) + "=" + kids[q] + " ";
        named.insert(Kids{s + "}"});
      }
      out = std::move(named);
    }
    busy.erase(key);
    memo[key] = out;
    return out;
  };
  run = [&](unsigned k, unsigned dot, unsigned i, unsigned j) -> Vals {
    const Prod& p = prods[k];
    if (dot == p.rhs.size()) return i == j ? Vals{Kids{}} : Vals{};
    const Sym& s = p.rhs[dot];
    if (s.kind == Sym::Kind::term)
      return i < j && d.toks[i] == s.tok ? run(k, dot + 1, i + 1, j) : Vals{};
    if (s.kind == Sym::Kind::cond)
      return holds(s, d, i) ? run(k, dot + 1, i, j) : Vals{};
    Vals out;
    for (unsigned m = i; m <= j; ++m) {
      if (!done.contains({s.nt, i, m})) continue;
      const Vals heads = span(s.nt, i, m);
      if (heads.empty()) continue;
      const Vals rests = run(k, dot + 1, m, j);
      for (const Kids& h : heads)
        for (const Kids& rest : rests) {
          Kids both = h;
          both.insert(both.end(), rest.begin(), rest.end());
          out.insert(std::move(both));
          if (out.size() > kCap) {
            over = true;
            return out;
          }
        }
    }
    return out;
  };
  const Vals trees = span(0, 0, n);
  if (!over && trees.size() == 1 && trees.begin()->size() == 1)
    v.tree = trees.begin()->front();
  return v;
}

}  // namespace cursiv
