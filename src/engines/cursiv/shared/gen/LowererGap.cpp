#include <functional>

#include "cursiv/grammar/expr.hpp"

#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/Lowerer.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/UnboundedCondition.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/Symbol.hpp"

namespace cursiv {

namespace {

constexpr std::string_view kUnreadable =
    "a condition (`&e` or `!e`) cannot test this expression: `e` may hold "
    "terminals, data rules that do not call themselves, classes, literals "
    "and `any`, their sequences, choices and repetitions, nested `&` and "
    "`!`, gap conditions (`gap::has`, `gap::is`, `eof`) and `skip()`, "
    "combined with `!`, `|` and `+`. A condition cannot test a rule that "
    "builds a node -- test the terminals it starts with";

bool gap_readable(const LowerPools& c, std::uint16_t n,
                  bool recognition = false) {
  const LexNode& e = c.lex[n];
  switch (e.kind) {
    case LexKind::token:
      return recognition && c.terms[e.a].laid == 0;
    case LexKind::positive:
    case LexKind::negative:
      return recognition && gap_readable(c, e.a, true);
    case LexKind::gap_has:
    case LexKind::gap_is:
      return recognition && gap_readable(c, e.a);
    case LexKind::end_mark:
      return recognition;
    case LexKind::rep:
      return gap_readable(c, e.a, recognition) &&
             (e.b == kNoLexNode || gap_readable(c, e.b, recognition));
    case LexKind::chars:
    case LexKind::nguard:
      return true;
    case LexKind::skip:
      if (!recognition) return false;
      [[fallthrough]];
    case LexKind::seq:
    case LexKind::alt:
      for (unsigned i = 0; i < e.b; ++i)
        if (!gap_readable(c, c.lexKid[e.a + i], recognition)) return false;
      return true;
    default:
      return false;
  }
}

}  // namespace

std::uint16_t Lowerer::gap_leaf(std::uint64_t id,
                                const std::function<GapLeaf()>& make) {
  const auto [at_, fresh] = _pool.gapLeaf.try_emplace(id, _c.ngapLeaves);
  if (!fresh) return at_->second;
  if (_c.ngapLeaves >= kGapAnd) {
    _pool.gapLeaf.erase(at_);
    lower_refuse(_c, LowerFail::gapLeafPool);
    return 0;
  }
  const GapLeaf leaf = make();
  if (leaf.kind != GapLeafKind::end && leaf.kind != GapLeafKind::cut &&
      (!gap_readable(_c, leaf.node, leaf.reads_tokens()) ||
       (leaf.skip != kNoLexNode && !gap_readable(_c, leaf.skip, true))))
    lower_refuse(_c, LowerFail::gapShape);
  _c.gapLeaves[_c.ngapLeaves] = leaf;
  return _c.ngapLeaves++;
}

bool Lowerer::lexical(detail::ImageId id) {
  if (const auto it = _lexicalMemo.find(id); it != _lexicalMemo.end())
    return it->second != 0;
  const detail::ImageNode n = at(id);
  bool out = false;
  switch (n.kind) {
    case detail::ImageKind::cpset:
    case detail::ImageKind::lit:
    case detail::ImageKind::byte:
    case detail::ImageKind::eof:
    case detail::ImageKind::through:
    case detail::ImageKind::sectionClose:
    case detail::ImageKind::gapHas:
    case detail::ImageKind::gapIs:
      out = true;
      break;
    case detail::ImageKind::andOf:
    case detail::ImageKind::skip:
    case detail::ImageKind::doc:
    case detail::ImageKind::trigger:
    case detail::ImageKind::notOf:
    case detail::ImageKind::repeat:
      out = lexical(kid(n, 0));
      break;
    case detail::ImageKind::seq:
    case detail::ImageKind::choice:
      out = std::ranges::all_of(kids_of(n),
                                [&](detail::ImageId k) { return lexical(k); });
      break;
    case detail::ImageKind::ref:
      out = terminal_rule(n.a) || data_rule(n.a);
      break;
    default:
      break;
  }
  _lexicalMemo.emplace(id, out ? 1 : 0);
  return out;
}

bool Lowerer::gap(detail::ImageId id) {
  if (const auto it = _gapMemo.find(id); it != _gapMemo.end())
    return it->second != 0;
  const detail::ImageNode n = at(id);
  bool out = false;
  switch (n.kind) {
    case detail::ImageKind::gapHas:
    case detail::ImageKind::gapIs:
      out = true;
      break;
    case detail::ImageKind::andOf:
      out = lexical(kid(n, 0));
      break;
    case detail::ImageKind::notOf: {
      const detail::ImageId k = kid(n, 0);
      if (const detail::ImageNode kn = at(k);
          kn.kind == detail::ImageKind::trigger) {
        const detail::ImageId p = kid(kn, 0);
        out = lexical(p) && !cut_prefix(p).ok;
      } else {
        out = gap(k) || lexical(k);
      }
      break;
    }
    case detail::ImageKind::seq:
    case detail::ImageKind::choice: {
      bool parts = true;
      bool any = false;
      for (const detail::ImageId k : kids_of(n)) {
        parts = parts && (gap(k) || at(k).kind == detail::ImageKind::eof);
        any = any || gap(k);
      }
      out = parts && any;
      break;
    }
    default:
      break;
  }
  _gapMemo.emplace(id, out ? 1 : 0);
  return out;
}

bool Lowerer::phrase_gap(detail::ImageId id) {
  const detail::ImageNode n = at(id);
  switch (n.kind) {
    case detail::ImageKind::andOf:
      return lexical(kid(n, 0));
    case detail::ImageKind::notOf:
      return phrase_gap(kid(n, 0)) || lexical(kid(n, 0));
    case detail::ImageKind::choice:
    case detail::ImageKind::seq:
      return std::ranges::any_of(
          kids_of(n), [&](detail::ImageId k) { return phrase_gap(k); });
    default:
      return false;
  }
}

bool Lowerer::lex_phrase(detail::ImageId id) {
  const detail::ImageNode n = at(id);
  switch (n.kind) {
    case detail::ImageKind::seq:
    case detail::ImageKind::andOf:
    case detail::ImageKind::notOf:
    case detail::ImageKind::eof:
    case detail::ImageKind::skip:
      return true;
    case detail::ImageKind::choice:
      return std::ranges::any_of(
          kids_of(n), [&](detail::ImageId k) { return lex_phrase(k); });
    case detail::ImageKind::repeat:
      return n.n > 1 || lex_phrase(kid(n, 0));
    case detail::ImageKind::ref:
      return data_rule(n.a);
    case detail::ImageKind::doc:
    case detail::ImageKind::trigger:
      return lex_phrase(kid(n, 0));
    default:
      return false;
  }
}

bool Lowerer::cut_one(detail::ImageId id) const {
  const detail::ImageNode& n = at(id);
  switch (n.kind) {
    case detail::ImageKind::lit:
    case detail::ImageKind::cpset:
      return true;
    case detail::ImageKind::ref:
      return terminal_rule(n.a);
    case detail::ImageKind::doc:
      return cut_one(kid(n, 0));
    default:
      return false;
  }
}

bool Lowerer::cut_tokens(detail::ImageId id) const {
  const detail::ImageNode& n = at(id);
  return n.kind != detail::ImageKind::choice
             ? cut_one(id)
             : std::ranges::all_of(
                   kids_of(n), [&](detail::ImageId k) { return cut_one(k); });
}

Lowerer::Cut Lowerer::cut_prefix(detail::ImageId p) {
  const detail::ImageNode n = at(p);
  const std::vector<detail::ImageId> es = n.kind == detail::ImageKind::seq
                                              ? kids_of(n)
                                              : std::vector<detail::ImageId>{p};
  Cut out;
  if (es.empty()) return out;
  for (std::size_t i = 0; i + 1 < es.size(); ++i) {
    if (!gap(es[i])) return {};
    out.guards.push_back(es[i]);
  }
  out.token = es.back();
  out.ok = cut_tokens(out.token);
  return out;
}

std::uint16_t Lowerer::skip_node(unsigned cfg) {
  if (_pool.skipNode.size() <= cfg) _pool.skipNode.resize(cfg + 1u, kNoLexNode);
  if (_pool.skipNode[cfg] != kNoLexNode) return _pool.skipNode[cfg];
  std::vector<std::uint16_t> bodies;
  for (std::uint32_t i = _c.cfgTriviaAt[cfg]; i < _c.cfgTriviaAt[cfg + 1u];
       ++i)
    bodies.push_back(_lex.push(LexNode{LexKind::token, _c.cfgTrivia[i]}));
  return _pool.skipNode[cfg] = _lex.group(LexKind::skip, bodies);
}

std::uint16_t Lowerer::phrase_lexical(unsigned cfg, detail::ImageId e) {
  if (at(e).kind == detail::ImageKind::epsilon)
    return _lex.push(LexNode{LexKind::seq, _c.nlexKid, 0});
  return _lex.reify(_lex.resolve(cfg, e));
}

std::uint16_t Lowerer::phrase_private_token(unsigned cfg, detail::ImageId e,
                                            detail::ImageId body) {
  const detail::ImageNode n = at(e);
  const auto [at_, fresh] = _pool.termIndex.try_emplace(
      key(Key::privateToken, _cfgs[cfg].home, e), _nterms);
  const std::uint16_t t = at_->second;
  if (fresh) {
    if (_nterms >= kNtBase) {
      _pool.termIndex.erase(at_);
      lower_refuse(_c, LowerFail::tooManyTerminals);
      return 0;
    }
    ++_nterms;
    _c.terms[t] = {};
    if (n.kind == detail::ImageKind::ref)
      name_row(_c.terms[t], _im.rules[n.a].name);
    _c.termBuild[t].lex = phrase_lexical(cfg, e);
    _c.termBuild[t].predicateOnly = true;
    const detail::ImageNode b = at(body);
    if (b.kind == detail::ImageKind::lit) spell_literal(t, b);
  }
  return _lex.push(LexNode{LexKind::token, t});
}

std::uint16_t Lowerer::phrase_token(unsigned cfg, detail::ImageId e) {
  const detail::ImageNode n = at(e);
  if (n.kind != detail::ImageKind::ref) return phrase_private_token(cfg, e, e);
  if (_termIdx.contains(n.a))
    return _lex.push(LexNode{LexKind::token, sym_of(cfg, n.a)});
  return phrase_private_token(cfg, e, body_in(cfg, n.a));
}

std::uint16_t Lowerer::phrase(unsigned cfg, detail::ImageId e,
                              std::uint16_t skip,
                              std::vector<std::uint32_t>& path) {
  const detail::ImageNode n = at(e);
  switch (n.kind) {
    case detail::ImageKind::seq: {
      std::vector<std::uint16_t> parts, row;
      for (const detail::ImageId k : kids_of(n))
        parts.push_back(phrase(cfg, k, skip, path));
      const bool spaced = _c.lex[skip].b != 0;
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (spaced && i != 0) row.push_back(phrase_skip(skip));
        row.push_back(parts[i]);
      }
      return _lex.group(LexKind::seq, row);
    }
    case detail::ImageKind::choice: {
      std::vector<std::uint16_t> kids;
      for (const detail::ImageId k : kids_of(n))
        kids.push_back(phrase(cfg, k, skip, path));
      return _lex.group(LexKind::alt, kids);
    }
    case detail::ImageKind::repeat: {
      if (n.m >= kEndless || (n.n != kUnbounded && n.n >= kEndless))
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      const auto one = phrase(cfg, kid(n, 0), skip, path);
      if (n.m == 1 && n.n == 1) return one;
      const auto between = _c.lex[skip].b == 0 || n.n <= 1
                               ? kNoLexNode
                               : phrase_skip(skip);
      return _lex.push(LexNode{LexKind::rep, one, between,
                               static_cast<std::uint32_t>(n.m),
                               n.n == kUnbounded
                                   ? kEndless
                                   : static_cast<std::uint32_t>(n.n)});
    }
    case detail::ImageKind::trigger: {
      const auto body = phrase(cfg, kid(n, 0), skip, path);
      if (lex_empty_nodes(_c)[body] != 0)
        lower_refuse_rule(_c, LowerFail::commitPrefixEmpty, _pool.owner);
      return body;
    }
    case detail::ImageKind::sectionClose: {
      if (_cfgs[cfg].end == detail::kNoImage)
        refuse_lowering("cursiv: a section's close is read outside any section");
      return phrase(cfg, _cfgs[cfg].end, skip, path);
    }
    case detail::ImageKind::andOf:
    case detail::ImageKind::notOf: {
      const auto body = phrase(cfg, kid(n, 0), skip, path);
      return _lex.push(LexNode{n.kind == detail::ImageKind::andOf
                                   ? LexKind::positive
                                   : LexKind::negative,
                               body});
    }
    case detail::ImageKind::eof:
      return _lex.push(LexNode{LexKind::end_mark});
    case detail::ImageKind::epsilon:
      return _lex.push(LexNode{LexKind::seq, _c.nlexKid, 0});
    case detail::ImageKind::gapHas:
    case detail::ImageKind::gapIs:
      return _lex.push(LexNode{n.kind == detail::ImageKind::gapHas
                                   ? LexKind::gap_has
                                   : LexKind::gap_is,
                               phrase_lexical(cfg, kid(n, 0))});
    case detail::ImageKind::doc:
      return phrase(cfg, kid(n, 0), skip, path);
    case detail::ImageKind::skip: {
      std::vector<std::uint16_t> trivia;
      for (std::uint64_t i = 0; i < n.m + n.n; ++i)
        trivia.push_back(phrase_token(
            cfg,
            _lex.make(detail::ImageKind::ref, {}, 0, _im.ruleLists[n.a + i])));
      return phrase(cfg, kid(n, 0), _lex.group(LexKind::skip, trivia), path);
    }
    case detail::ImageKind::ref: {
      if (terminal_rule(n.a)) return phrase_token(cfg, e);
      if (!data_rule(n.a))
        refuse_lowering(
            "a condition cannot test a rule that builds a node -- test the "
            "terminals it starts with");
      if (std::ranges::find(path, n.a) != path.end())
        refuse_lowering(
            "a condition cannot test a data rule that calls itself, directly "
            "or through other rules -- test the terminals it starts with");
      path.push_back(n.a);
      const std::uint16_t out =
          phrase(cfg, body_in(cfg, n.a), skip, path);
      path.pop_back();
      return out;
    }
    default:
      return phrase_token(cfg, e);
  }
}

void Lowerer::cut_terms(unsigned cfg, detail::ImageId token, DynSet& out) {
  if (const detail::ImageNode n = at(token);
      n.kind == detail::ImageKind::choice) {
    for (const detail::ImageId k : kids_of(n)) cut_terms(cfg, k, out);
    return;
  }
  out.set(term_of(cfg, token));
}

std::uint16_t Lowerer::cut_node(unsigned cfg, detail::ImageId token) {
  DynSet terms;
  cut_terms(cfg, token, terms);
  std::vector<std::uint16_t> kids;
  for (std::uint16_t t = 0; t < _nterms; ++t)
    if (terms.test(t))
      kids.push_back(_lex.push(LexNode{LexKind::token, t}));
  return kids.size() == 1 ? kids.front() : _lex.group(LexKind::alt, kids);
}

void Lowerer::gap_join(unsigned cfg, std::uint16_t op,
                       const std::vector<detail::ImageId>& es) {
  for (const detail::ImageId e : es) gap_ops(cfg, e);
  gap_op(op);
  gap_op(static_cast<std::uint16_t>(es.size()));
}

void Lowerer::gap_ops(unsigned cfg, detail::ImageId e) {
  const detail::ImageNode n = at(e);
  switch (n.kind) {
    case detail::ImageKind::gapHas:
    case detail::ImageKind::gapIs: {
      const bool has = n.kind == detail::ImageKind::gapHas;
      gap_op(gap_leaf(key(has ? Key::hasLeaf : Key::isLeaf, 0, e), [&] {
        return GapLeaf{.kind = has ? GapLeafKind::has : GapLeafKind::is,
                       .node = phrase_lexical(cfg, kid(n, 0))};
      }));
      return;
    }
    case detail::ImageKind::andOf: {
      const detail::ImageId k = kid(n, 0);
      const detail::ImageNode kn = at(k);
      if (kn.kind == detail::ImageKind::eof ||
          kn.kind == detail::ImageKind::notOf) {
        gap_ops(cfg, k);
        return;
      }
      if (kn.kind == detail::ImageKind::trigger) {
        const Cut cut = cut_prefix(kid(kn, 0));
        if (cut.ok) {
          const detail::ImageId trigger =
              _lex.make(detail::ImageKind::trigger, std::span(&cut.token, 1));
          gap_op(gap_leaf(key(Key::cutLeaf, cfg, trigger), [&] {
            return GapLeaf{.kind = GapLeafKind::cut,
                           .node = cut_node(cfg, cut.token)};
          }));
          for (const detail::ImageId g : cut.guards) gap_ops(cfg, g);
          if (!cut.guards.empty()) {
            gap_op(kGapAnd);
            gap_op(static_cast<std::uint16_t>(cut.guards.size() + 1u));
          }
          return;
        }
      }
      if (!lexical(k)) refuse_lowering(kUnreadable);
      gap_op(gap_leaf(key(Key::andLeaf, cfg, e), [&] {
        const std::uint16_t skip = skip_node(cfg);
        std::vector<std::uint32_t> path;
        const std::uint16_t body = phrase(cfg, k, skip, path);
        return GapLeaf{
            .kind = lex_phrase(k) ? GapLeafKind::phrase : GapLeafKind::head,
            .node = body,
            .skip = _c.lex[skip].b == 0 ? kNoLexNode : phrase_skip(skip),
            .cfg = kn.kind == detail::ImageKind::sectionClose
                       ? std::uint8_t{0}
                       : static_cast<std::uint8_t>(cfg),
            .trigger = kn.kind == detail::ImageKind::trigger};
      }));
      return;
    }
    case detail::ImageKind::eof:
      gap_op(gap_leaf(key(Key::endLeaf, 0, e),
                      [] { return GapLeaf{.kind = GapLeafKind::end}; }));
      return;
    case detail::ImageKind::notOf: {
      detail::ImageId inner = kid(n, 0);
      if (at(inner).kind == detail::ImageKind::andOf) inner = kid(at(inner), 0);
      if (gap(inner) || at(inner).kind == detail::ImageKind::eof)
        gap_ops(cfg, inner);
      else
        gap_ops(cfg, _lex.make(detail::ImageKind::andOf, std::span(&inner, 1)));
      gap_op(kGapNot);
      gap_op(1);
      return;
    }
    case detail::ImageKind::choice:
      gap_join(cfg, kGapOr, kids_of(n));
      return;
    case detail::ImageKind::seq:
      gap_join(cfg, kGapAnd, kids_of(n));
      return;
    default:
      refuse_lowering(kUnreadable);
  }
}

std::uint16_t Lowerer::gap_cond(unsigned cfg, detail::ImageId e) {
  const std::uint64_t k = phrase_gap(e) ? key(Key::phrase, cfg, e)
                                        : key(Key::cond, 0, e);
  const auto [at_, fresh] = _pool.gapCond.try_emplace(k, _c.ngapConds);
  const std::uint16_t out = at_->second;
  if (!fresh) return out;
  if (_c.ngapConds >= kGapConds) {
    _pool.gapCond.erase(at_);
    lower_refuse(_c, LowerFail::gapTooMany);
    return 0;
  }
  _c.gapCondAt[_c.ngapConds] = _c.ngapOps;
  gap_ops(cfg, e);
  ++_c.ngapConds;
  _c.gapCondAt[_c.ngapConds] = _c.ngapOps;
  find_unbounded_runs(_c, out, _pool.owner, _c.unbounded);
  return out;
}

void Lowerer::check_implied_token(unsigned cfg, detail::ImageId e) {
  const detail::ImageNode n = at(e);
  if (n.kind == detail::ImageKind::doc)
    return check_implied_token(cfg, kid(n, 0));
  if (n.kind == detail::ImageKind::ref && terminal_rule(n.a))
    return check_implied_token(cfg, body_in(cfg, n.a));
  if (n.kind != detail::ImageKind::lit ||
      (n.flags & (detail::kLitGuardWord | detail::kLitGuardExpr)))
    refuse_lowering(
        "in `implied(tok, cond)`, `tok` must be a literal with no guard, or "
        "a terminal whose body is one: the parser inserts it with zero width "
        "when it is missing -- imply a plain `lit<>`, or write `tok | eof` "
        "where the token may be absent");
  _impliedToks.insert(_lex.canon(e));
}

std::uint16_t Lowerer::implied_term(detail::ImageId e, std::uint16_t k) {
  const std::uint16_t before = _nterms;
  // One terminal per condition: a phrase's differs with the trivia around it.
  const std::uint16_t t =
      intern_spelled(key(Key::implied, k, e), "implied", false, false, [&] {
        return _lex.push(LexNode{LexKind::implied, 0, 0, 0, 0});
      });
  if (t == before) {
    unspell(t, kLaidImplied);
    _c.terms[t].cond = k;
  }
  return t;
}

}  // namespace cursiv
