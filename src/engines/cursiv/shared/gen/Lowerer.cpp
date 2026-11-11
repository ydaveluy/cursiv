#include "cursiv/shared/gen/Lowerer.hpp"

#include <algorithm>
#include <functional>
#include <span>
#include <string>

#include "cursiv/grammar/expr.hpp"
#include "cursiv/shared/gen/AstRegistryBuilder.hpp"
#include "cursiv/shared/gen/LevelFault.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/WrittenReads.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/utils/ClassSpelling.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/GrammarPrinter.hpp"
#include "cursiv/shared/utils/white_space.hpp"

namespace cursiv::engine {

namespace {

template <class T>
bool has(const std::vector<T>& sorted, T v) {
  return std::ranges::binary_search(sorted, v);
}

std::vector<std::uint32_t> sorted_unique(std::vector<std::uint32_t> v) {
  std::ranges::sort(v);
  v.erase(std::ranges::unique(v).begin(), v.end());
  return v;
}

}  // namespace

Lowerer::Lowerer(GrammarImage& image, const LowerDesc& desc, LowerPools& pools,
                 std::uint16_t& nterms, ByteSet& wordBytes, ChainPool& writes,
                 GrammarFault& fault, LowerPool& pool)
    : _im(image),
      _twins(image,
             [this](std::uint32_t rule) {
               return data_rule(rule) && !_im.rules[rule].foreign
                          ? _im.node(*stage(rule).body, _im.folds(rule))
                          : kNoImage;
             }),
      _d(desc),
      _c(pools),
      _nterms(nterms),
      _writes(writes),
      _fault(fault),
      _pool(pool),
      _lex(*this, wordBytes) {}

std::vector<ImageId> Lowerer::children_of(const ImageNode& n) const {
  return {_im.children.begin() + n.first,
          _im.children.begin() + n.first + n.count};
}

std::uint64_t Lowerer::key(Key kind, unsigned cfg, ImageId node) {
  return std::uint64_t{static_cast<std::uint8_t>(kind)} << 56 |
         std::uint64_t{cfg} << 32 | _lex.canon(node);
}

std::uint16_t Lowerer::row_id(const void* row) const {
  const auto it = _rowIdx.find(row);
  if (it == _rowIdx.end())
    refuse_lowering("the lowering requested an action it did not publish");
  return it->second;
}

std::uint8_t Lowerer::role_in(unsigned cfg, std::uint32_t rule) const {
  if (has(_cfgs[cfg].ignore, rule)) return 1;
  if (has(_cfgs[cfg].hide, rule)) return 2;
  return 0;
}

std::uint16_t Lowerer::sym_of(unsigned cfg, std::uint32_t rule) {
  if (const auto t = _termIdx.find(rule); t != _termIdx.end()) {
    if (const std::uint8_t role = role_in(cfg, rule);
        role != role_in(0, rule)) {
      const auto at = std::ranges::find(
          _roles, std::pair<std::uint32_t, std::uint8_t>{rule, role});
      return role_sym(static_cast<std::size_t>(at - _roles.begin()));
    }
    return static_cast<std::uint16_t>(1 + t->second);
  }
  if (const auto n = _ntIdx.find(rule); n != _ntIdx.end())
    return nt_sym(
        static_cast<std::uint16_t>(1 + n->second + cfg * _d.nts.size()));
  refuse_lowering("the lowering names a rule it did not number");
}

ImageId Lowerer::body_in(unsigned cfg, std::uint32_t rule) {
  const bool section = _cfgs[cfg].end != kNoImage;
  const std::uint64_t k = std::uint64_t{rule} << 1 | (section ? 1u : 0u);
  if (const auto it = _bodies.find(k); it != _bodies.end()) return it->second;
  ImageId out = _im.node(*stage(rule).body, _im.folds(rule));
  if (data_rule(rule) || node_rule(rule)) out = _twins.drop(out);
  if (section) out = descend(ended(out));
  _bodies.emplace(k, out);
  return out;
}

bool Lowerer::reads_under(unsigned cfg, std::uint32_t rule) const {
  const ImageRuleDesc* const ownerGrammar = _im.rules[rule].desc->ownerGrammar;
  const std::uint32_t section = _cfgs[cfg].section;
  if (ownerGrammar == nullptr) return section == kNoImage;
  return section != kNoImage && _im.rules[section].desc == ownerGrammar;
}

unsigned Lowerer::region_of(unsigned cfg, const ImageNode& skip) const {
  const auto lists = _im.ruleLists.begin() + skip.a;
  const auto ignore =
      sorted_unique(std::vector<std::uint32_t>(lists, lists + skip.m));
  const auto hide = sorted_unique(
      std::vector<std::uint32_t>(lists + skip.m, lists + skip.m + skip.n));
  const Config& config = _cfgs[cfg];
  for (unsigned k = 0; k < _cfgs.size(); ++k)
    if (_cfgs[k].section == config.section && _cfgs[k].end == config.end &&
        _cfgs[k].ignore == ignore && _cfgs[k].hide == hide)
      return k;
  refuse_lowering("the lowering reads a region no configuration holds");
}

unsigned Lowerer::section_of(std::uint32_t grammar, ImageId end) const {
  for (unsigned k = 0; k < _cfgs.size(); ++k)
    if (_cfgs[k].section == grammar && _cfgs[k].end == end) return k;
  refuse_lowering("the lowering reads a section no configuration holds");
}

ImageId Lowerer::section_end() {
  const ImageId close = _lex.make(ImageKind::sectionClose);
  const ImageId hold = _lex.make(ImageKind::andOf, std::span(&close, 1));
  const ImageId eof = _lex.make(ImageKind::eof);
  const ImageId arms[] = {hold, eof};
  return _lex.make(ImageKind::choice, arms);
}

ImageId Lowerer::token_core(ImageId id) const {
  for (;; id = child_at(at(id), 0)) {
    const ImageNode& n = at(id);
    switch (n.kind) {
      case ImageKind::lit:
        return id;
      case ImageKind::ref:
        return terminal_rule(n.a) ? id : kNoImage;
      case ImageKind::doc:
      case ImageKind::assign:
      case ImageKind::append:
        continue;
      default:
        return kNoImage;
    }
  }
}

void Lowerer::index() {
  const auto rules = [&](std::span<const ImageRuleDesc* const> list) {
    std::vector<std::uint32_t> out;
    for (const ImageRuleDesc* const r : list) out.push_back(_im.rule(*r));
    return out;
  };
  _termRule = rules(_d.terms);
  _ntRule = rules(_d.nts);
  for (std::uint32_t i = 0; i < _termRule.size(); ++i)
    _termIdx.emplace(_termRule[i], i);
  for (std::uint32_t i = 0; i < _ntRule.size(); ++i)
    _ntIdx.emplace(_ntRule[i], i);
  _predicate = sorted_unique(rules(_d.predicateTerms));
  for (std::uint32_t i = 0; i < _d.rows.size(); ++i) {
    _rowIdx.emplace(_d.rows[i], static_cast<std::uint16_t>(i));
    _rowKind.push_back(_d.rows[i]->gen.kind);
  }
  open_configs();
  for (const std::uint32_t rule : _skipped)
    if (!_termIdx.contains(rule))
      refuse_lowering(
          "`ignored`, `hidden` and `skip()` list terminals only: trivia are "
          "tokens the lexer skips, never rules");
  for (const Config& c : _cfgs)
    if (std::ranges::any_of(c.ignore,
                            [&](std::uint32_t r) { return has(c.hide, r); }))
      refuse_lowering(
          "a terminal belongs to at most one of `ignored` and `hidden`: "
          "trivia are dropped from the tree or kept in it, never both");
  for (std::uint8_t role = 0; role < 3; ++role)
    for (const std::uint32_t rule : _skipped) {
      if (role_in(0, rule) == role) continue;
      for (unsigned k = 1; k < _cfgs.size(); ++k)
        if (role_in(k, rule) == role) {
          _roles.emplace_back(rule, role);
          break;
        }
    }
}

void Lowerer::open_configs() {
  const auto listed = [&](std::uint32_t at, std::uint64_t count) {
    return std::vector<std::uint32_t>(_im.ruleLists.begin() + at,
                                      _im.ruleLists.begin() + at + count);
  };
  std::vector<std::uint32_t> ignored, hidden;
  const auto push = [&](Config c, const std::vector<std::uint32_t>& ignore,
                        const std::vector<std::uint32_t>& hide) {
    for (const auto& [seen, rules] :
         {std::pair{&ignored, &ignore}, std::pair{&hidden, &hide}})
      for (const std::uint32_t rule : *rules)
        if (std::ranges::find(*seen, rule) == seen->end())
          seen->push_back(rule);
    _cfgs.push_back(std::move(c));
    return static_cast<unsigned>(_cfgs.size() - 1);
  };
  const auto push_listed = [&](Config c, const std::vector<std::uint32_t>& ignore,
                               const std::vector<std::uint32_t>& hide) {
    c.ignore = sorted_unique(ignore);
    c.hide = sorted_unique(hide);
    return push(std::move(c), ignore, hide);
  };
  push_listed({}, listed(_im.ignoreAt, _im.ignoreCount),
              listed(_im.hiddenAt, _im.hiddenCount));

  std::function<Descent<void>(ImageId, unsigned)> walk;
  const auto owned = [&](unsigned sectionCfg) -> Descent<void> {
    for (const std::uint32_t rule : _ntRule) {
      if (!reads_under(sectionCfg, rule)) continue;
      const ImageStageDesc& st = stage(rule);
      if (st.kind != ImageRuleKind::infix) {
        co_await walk(_im.node(*st.body, _im.folds(rule)), sectionCfg);
        continue;
      }
      for (std::uint32_t i = 0; i < st.levelCount; ++i)
        for (std::uint32_t j = 0; j < st.levels[i].count; ++j)
          co_await walk(_im.node(*st.levels[i].ops[j].op, _im.folds(rule)),
                        sectionCfg);
    }
  };
  // The text each documented word was first given.
  std::unordered_map<std::string_view, std::string_view> docOf;
  walk = [&](ImageId id, unsigned sectionCfg) -> Descent<void> {
    const ImageNode n = at(id);
    if (n.kind == ImageKind::doc && at(child_at(n, 0)).kind == ImageKind::lit) {
      const std::string_view word = at(child_at(n, 0)).text;
      const auto [text, fresh] = docOf.try_emplace(word, n.text);
      if (!fresh && text->second != n.text) _docsDiffer.insert(word);
    } else if (n.kind == ImageKind::skip) {
      const auto ignore = listed(n.a, n.m),
                 hide = listed(static_cast<std::uint32_t>(n.a + n.m), n.n);
      Config c;
      c.ignore = sorted_unique(ignore);
      c.hide = sorted_unique(hide);
      if (std::ranges::none_of(_cfgs, [&](const Config& o) {
            return o.sectionCfg == sectionCfg && o.ignore == c.ignore &&
                   o.hide == c.hide;
          })) {
        c.sectionCfg = sectionCfg;
        if (sectionCfg != 0) {
          c.section = _cfgs[sectionCfg].section;
          c.end = _cfgs[sectionCfg].end;
        }
        push(std::move(c), ignore, hide);
      }
    } else if ((n.kind == ImageKind::andOf || n.kind == ImageKind::notOf) &&
               co_await holds(id, [](const ImageNode& k) {
                 return k.kind == ImageKind::embed;
               })) {
      refuse_lowering(
          "`section<G>(open, close)` is allowed in a rule, not in a condition: a "
          "condition tests text and cannot parse a grammar -- test the token "
          "that opens the section");
    } else if (n.kind == ImageKind::embed &&
               at(child_at(n, 0)).kind == ImageKind::seq &&
               at(child_at(n, 0)).count == 3) {
      const ImageNode inner = at(child_at(n, 0));
      const ImageId end = token_core(child_at(inner, 2));
      if (end == kNoImage || token_core(child_at(inner, 0)) == kNoImage)
        refuse_lowering(
            "in `section<G>(open, close)`, `open` and `close` must each be "
            "one token of the calling grammar: a literal, a keyword or a "
            "terminal");
      co_await walk(child_at(inner, 0), sectionCfg);
      if (std::ranges::none_of(_cfgs, [&](const Config& c) {
            return c.section == n.a && c.end == end;
          })) {
        const ImageFace& face = _im.ready(n.a).face[0];
        Config c;
        c.section = n.a;
        c.end = end;
        c.bound = face.bound;
        c.sectionCfg = static_cast<std::uint32_t>(_cfgs.size());
        co_await owned(push_listed(std::move(c),
                                   listed(face.ignoreAt, face.ignoreCount),
                                   listed(face.hiddenAt, face.hiddenCount)));
      }
      co_await walk(child_at(inner, 2), sectionCfg);
      co_return;
    }
    // `owned` may grow the image: the children are read again after each walk.
    for (std::size_t i = 0, count = _im.shape_children(id).size(); i < count;
         ++i)
      co_await walk(_im.shape_children(id)[i], sectionCfg);
  };
  descend(owned(0));

  _skipped = ignored;
  for (const std::uint32_t rule : hidden)
    if (std::ranges::find(_skipped, rule) == _skipped.end())
      _skipped.push_back(rule);
}

void Lowerer::spell_literal(std::uint16_t t, const ImageNode& lit) {
  _c.terms[t].textOff = intern_name(lit.text);
  _c.terms[t].textLen = static_cast<std::uint16_t>(lit.text.size());
  _c.termBuild[t].folded = (lit.flags & kLitFolded) != 0;
  _c.terms[t].spelled = true;
}

// The words a choice of them reads, each once, in the order written: literals
// with no guard of their own, and keywords. False where it reads anything
// else, white space included: a line break is no word to offer.
Descent<bool> Lowerer::list_words(ImageId id,
                                  std::vector<std::string_view>& words) const {
  const ImageNode& n = at(id);
  if (n.kind == ImageKind::lit) {
    if ((n.flags & kLitGuardExpr) != 0 || white_space_only(n.text))
      co_return false;
    if (std::ranges::find(words, n.text) == words.end())
      words.push_back(n.text);
    co_return true;
  }
  if (n.kind != ImageKind::choice) co_return false;
  for (const ImageId k : children_of(n))
    if (!co_await list_words(k, words)) co_return false;
  co_return true;
}

void Lowerer::declare_term(std::uint32_t rule) {
  const ImageStageDesc& st = stage(rule);
  const ImageId bodyId = body_in(0, rule);
  const ImageNode body = at(bodyId);
  const std::uint16_t t = _nterms++;
  name_row(_c.terms[t], _im.rules[rule].name);
  if (body.kind == ImageKind::lit) spell_literal(t, body);
  // A choice of words spells none of them alone, but each is a spelling.
  if (std::vector<std::string_view> words;
      body.kind == ImageKind::choice && descend(list_words(bodyId, words)))
    _c.termBuild[t].words = std::move(words);
  const bool predicateOnly = has(_predicate, rule);
  if (!predicateOnly) {
    const std::uint16_t conv = row_id(st.convertRow);
    _c.terms[t].element = _writes.intern_chain(&conv, 1);
  }
  _c.terms[t].recorded = has(_cfgs[0].hide, rule);
  _c.terms[t].trivia = _c.terms[t].recorded || has(_cfgs[0].ignore, rule);
  _c.termBuild[t].predicateOnly = predicateOnly;
  const ImageId lexBody = _lex.terminal_body(rule, bodyId);
  const std::uint32_t path[] = {rule};
  _c.termBuild[t].lex = _lex.reify(_lex.resolve(0, lexBody, path));
  if (body.kind == ImageKind::indent) _c.terms[t].synthetic = kSyntheticIndent;
  if (body.kind == ImageKind::dedent) _c.terms[t].synthetic = kSyntheticDedent;
}

void Lowerer::declare_role_term(std::size_t at) {
  const auto [rule, role] = _roles[at];
  const std::uint16_t from = sym_of(0, rule);
  const TermInfo term = _c.terms[from];
  const std::uint16_t to = _nterms++;
  _c.terms[to] = term;
  _c.termBuild[to] = TermBuild(_c.termBuild[from]);
  _c.terms[to].trivia = role != 0;
  _c.terms[to].recorded = role == 2;
  _c.cloneBase[to] = from;
}

void Lowerer::inherit_role_term(std::size_t at) {
  const std::uint32_t rule = _roles[at].first;
  const TermInfo base = _c.terms[sym_of(0, rule)];
  TermInfo& copy = _c.terms[role_sym(at)];
  copy.delim = copy.trivia ? std::int8_t{0} : base.delim;
  copy.joins = base.joins;
}

void Lowerer::declare_config(unsigned k) {
  const Config& cf = _cfgs[k];
  _c.cfgTriviaAt[k] = static_cast<std::uint32_t>(_c.cfgTrivia.size());
  bool shortened = false;
  for (const std::uint32_t rule : _skipped) {
    if (role_in(k, rule) != 0)
      _c.cfgTrivia.v.push_back(sym_of(k, rule));
    else
      shortened |= role_in(cf.sectionCfg, rule) != 0;
  }
  _c.cfgSection[k] = static_cast<std::uint8_t>(cf.sectionCfg);
  _c.cfgBare[k] = k != cf.sectionCfg && cf.ignore.empty() && cf.hide.empty();
  _c.cfgShort[k] = k != cf.sectionCfg && shortened;
}

void Lowerer::delimit_spelled(ImageId spelled, std::int8_t d) {
  const ImageId want = _lex.canon(spelled);
  if (_impliedToks.contains(want))
    refuse_lowering(
        "in `implied(tok, cond)`, `tok` cannot be a bracket named by "
        "`indent_open` or `indent_close` -- write the bracket in the text");
  for (const std::uint32_t rule : _termRule)
    if (_lex.canon(body_in(0, rule)) == want &&
        !_c.terms[sym_of(0, rule)].trivia)
      _c.terms[sym_of(0, rule)].delim = d;
}

Descent<void> Lowerer::delimit(ImageId spelled, std::int8_t d) {
  const ImageNode n = at(spelled);
  switch (n.kind) {
    case ImageKind::epsilon:
      co_return;
    case ImageKind::lit:
      if (const auto t = _pool.termIndex.find(
              key(Key::token, 0, _lex.token_of(0, spelled, _d.bound)));
          t != _pool.termIndex.end())
        _c.terms[t->second].delim = d;
      delimit_spelled(spelled, d);
      co_return;
    case ImageKind::ref: {
      if (!_termIdx.contains(n.a))
        refuse_lowering(
            "`indent_open` and `indent_close` must name terminals that a "
            "rule uses, not rules or unused terminals");
      if (const ImageId body = body_in(0, n.a); at(body).kind == ImageKind::lit)
        co_await delimit(body, d);
      else if (!_c.terms[sym_of(0, n.a)].trivia)
        _c.terms[sym_of(0, n.a)].delim = d;
      co_return;
    }
    case ImageKind::choice:
      for (const ImageId k : children_of(n)) co_await delimit(k, d);
      co_return;
    default:
      refuse_lowering(
          "`indent_open` and `indent_close` must name literals or "
          "terminals: `lit<\"(\">`, `ref<LPAR>` or a choice of them");
  }
}

void Lowerer::declare() {
  if (_d.terms.size() + _roles.size() >= kNtBase ||
      _d.nts.size() * _cfgs.size() >= kNtBase)
    refuse_lowering(
        "the grammar declares more than 32 767 terminals or rules, counting "
        "a copy of each rule for each trivia setting -- simplify the "
        "grammar, or split it");
  if (_cfgs.size() > 255) lower_refuse(_c, LowerFail::tooManyConfigurations);
  _nterms = 1;
  _c.terms[0].nameOff = intern_name("$end");
  _c.terms[0].nameLen = 4;
  declare_start();
  for (unsigned k = 0; k < _cfgs.size(); ++k)
    if (_cfgs[k].section != kNoImage && _cfgs[k].sectionCfg == k)
      _lex.declare_section_word(_cfgs[k].bound);
  for (const std::uint32_t rule : _termRule) declare_term(rule);
  for (std::size_t i = 0; i < _roles.size(); ++i) declare_role_term(i);
  for (unsigned k = 0; k < _cfgs.size(); ++k) declare_config(k);
  _c.cfgTriviaAt[_cfgs.size()] =
      static_cast<std::uint32_t>(_c.cfgTrivia.size());
  if (_d.indentNewline != nullptr) {
    const std::uint32_t newline = _im.rule(*_d.indentNewline);
    if (!_termIdx.contains(newline))
      refuse_lowering(
          "`indent_newline` must name the terminal that ends a line, one "
          "that a rule uses, not a rule or an unused terminal");
    _c.terms[sym_of(0, newline)].joins = true;
  }
  declare_rules();
}

bool Lowerer::clone_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs) {
  if (!_c.cloneNeeded[lhs] || _c.cloneDone[lhs]) return false;
  _c.cloneDone[lhs] = true;
  define_one(cfg, rule, lhs, 0);
  return true;
}

// The repetition of an unordered item the text may leave out, `option(x)`,
// `many(x)` or `repeat<0, n>(x)` seen through regions only; else `kNoImage`.
ImageId Lowerer::optional_item(ImageId item) const {
  while (at(item).kind == ImageKind::skip) item = child_at(at(item), 0);
  const ImageNode& n = at(item);
  return n.kind == ImageKind::repeat && n.m == 0 && n.n != 0 ? item : kNoImage;
}

// An unordered item and a repetition's item must read input: one that may read
// nothing could stand anywhere. An optional item is read in its present form,
// in a group with no `nest`. The first item refused is named.
void Lowerer::refuse_empty_items() {
  if (_fault) return;
  WrittenReads reads(_im);
  std::vector<ImageId> todo;
  GrammarFault repeated;
  for (std::size_t i = 0; i < _ntRule.size(); ++i) {
    const ImageFace face = _im.ready(_ntRule[i]).face[1];
    if (face.kind != ImageRuleKind::data && face.kind != ImageRuleKind::node)
      continue;
    todo.assign(1, face.body);
    while (!todo.empty()) {
      const ImageId id = todo.back();
      todo.pop_back();
      // An answer may build the faces of the rules it reaches: read by index.
      const ImageNode n = at(id);
      if (n.kind == ImageKind::andOf || n.kind == ImageKind::notOf) continue;
      const std::span<const ImageId> children = _im.shape_children(id);
      todo.insert(todo.end(), children.rbegin(), children.rend());
      if (n.kind == ImageKind::repeat && n.n > 1 &&
          reads.nothing(child_at(n, 0)))
        repeated.raise(GrammarFaultCode::nullableRepetition,
                       static_cast<std::uint16_t>(i + 1));
      if (n.kind != ImageKind::unordered) continue;
      const bool nests = descend(holds(
          id, [](const ImageNode& x) { return x.kind == ImageKind::nest; }));
      for (std::uint32_t k = 0; k < n.count; ++k) {
        const ImageId item = child_at(n, k);
        const ImageId optional = optional_item(item);
        const bool present =
            optional != kNoImage && !reads.nothing(child_at(at(optional), 0));
        if (present ? !nests : !reads.nothing(item)) continue;
        GrammarPrinter printer(_im, ImageView::resolved);
        printer.prime();
        _fault = GrammarFault{.code = GrammarFaultCode::nullableUnorderedItem,
                              .rule = static_cast<std::uint16_t>(i + 1),
                              .gated = present,
                              .part = printer.spell(item)};
        return;
      }
    }
  }
  _fault = repeated;
}

void Lowerer::define() {
  refuse_empty_items();
  if (!_fault) _fault = level_fault(_im, _ntRule);
  std::uint16_t lhs = 0;
  for (const std::uint32_t rule : _ntRule) {
    ++lhs;
    if (reads_under(0, rule)) define_one(0, rule, lhs, 0);
  }
  for (bool grew = _cfgs.size() > 1; grew;) {
    grew = define_pass(
        1, [this](auto k, auto rule, auto i) { return clone_one(k, rule, i); });
    grew = define_open() || grew;
  }
  // The brackets once the rules numbered what they read: a spelling no rule
  // reads is no terminal of its own.
  descend(delimit(_im.node(*_d.indentOpen, _im.caseInsensitive), 1));
  descend(delimit(_im.node(*_d.indentClose, _im.caseInsensitive), -1));
  for (std::size_t i = 0; i < _roles.size(); ++i) inherit_role_term(i);
}

void Lowerer::unspell(std::uint16_t t, std::uint8_t synthetic) {
  _c.terms[t].spelled = false;
  _c.terms[t].textLen = 0;
  _c.terms[t].synthetic = synthetic;
}

std::uint16_t Lowerer::intern_spelled(
    std::uint64_t id, std::string_view text, bool charset, bool folded,
    const std::function<std::uint16_t()>& lex) {
  const auto [at, fresh] = _pool.termIndex.try_emplace(id, _nterms);
  const std::uint16_t t = at->second;
  if (!fresh) return t;
  if (_nterms >= kNtBase) {
    _pool.termIndex.erase(at);
    lower_refuse(_c, LowerFail::tooManyTerminals);
    return 0;
  }
  ++_nterms;
  const std::uint16_t name = intern_name(text);
  _c.terms[t].nameOff = name;
  _c.terms[t].nameLen = static_cast<std::uint16_t>(text.size());
  _c.terms[t].textOff = name;
  _c.terms[t].textLen = _c.terms[t].nameLen;
  _c.termBuild[t].lex = lex();
  _c.termBuild[t].folded = folded;
  _c.termBuild[t].charset = charset;
  _c.terms[t].spelled = true;
  return t;
}

std::uint16_t Lowerer::term_of(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::lit: {
      const ImageId token = _lex.token_of(cfg, e, _d.bound);
      return intern_spelled(key(Key::token, 0, token), n.text, false,
                            (n.flags & kLitFolded) != 0,
                            [&] { return _lex.reify(_lex.resolve(cfg, e)); });
    }
    case ImageKind::cpset: {
      const bool complement = (n.flags & kCpNot) != 0;
      const bool plain = n.m == 0 && !complement && cp_ascii_of(n.text);
      std::string name;
      if (plain) {
        name = std::string(n.text);
      } else {
        cp_name_emit(n.m, complement, n.text, [&name](char c) { name += c; });
      }
      return intern_spelled(key(Key::token, 0, e), name, true, false,
                            [&] { return _lex.reify(e); });
    }
    case ImageKind::ref:
      return sym_of(cfg, n.a);
    case ImageKind::doc:
      return term_of(cfg, child_at(n, 0));
    default:
      refuse_lowering("this expression is no single token");
  }
}

std::uint16_t Lowerer::add_synthetic(ImageId e, std::uint8_t synthetic,
                                     std::string_view name) {
  std::uint16_t t = 1;
  while (t < _nterms && _c.terms[t].synthetic != synthetic) ++t;
  if (t == _nterms) {
    t = intern_spelled(key(Key::token, 0, e), name, false, false,
                       [&] { return _lex.reify(e); });
    unspell(t, synthetic);
  }
  return t;
}

std::uint16_t Lowerer::terminal_sym(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::lit:
      return term_of(cfg, e);
    case ImageKind::cpset:
      if (n.m == 0 && (n.flags & kCpNot) != 0 && n.text.empty())
        return intern_spelled(key(Key::token, 0, e), ".", true, false,
                              [&] { return _lex.reify(e); });
      return term_of(cfg, e);
    case ImageKind::byte:
      return intern_spelled(key(Key::token, 0, e), "\\C", true, false,
                            [&] { return _lex.reify(e); });
    case ImageKind::doc: {
      if (at(child_at(n, 0)).kind != ImageKind::lit)
        refuse_lowering(
            "a `doc` goes on a word a reader can hover, such as "
            R"(`kw<"if">` or `lit<"if">`: only a token shows hover )"
            "text, so a `doc` cannot go on a rule reference or on another "
            "expression");
      const std::uint16_t sym = terminal_sym(cfg, child_at(n, 0));
      if (!docs_differ(n)) _c.termBuild[sym].doc = n.text;
      return sym;
    }
    case ImageKind::through: {
      const ImageId run = _lex.resolve(cfg, e);
      const std::uint16_t t =
          intern_spelled(key(Key::through, 0, run), "through", true, false,
                         [&] { return _lex.reify(run); });
      if (t != 0) unspell(t, 0);
      return t;
    }
    case ImageKind::indent:
      return add_synthetic(e, kSyntheticIndent, "indent");
    case ImageKind::dedent:
      return add_synthetic(e, kSyntheticDedent, "dedent");
    default:
      refuse_lowering(kNotInRule);
  }
}

std::vector<ImageId> Lowerer::gap_runs(const std::vector<ImageId>& children) {
  std::vector<ImageId> out;
  std::vector<ImageId> run;
  const auto flush = [&] {
    if (run.size() == 1)
      out.push_back(run.front());
    else if (run.size() > 1)
      out.push_back(_lex.make(ImageKind::seq, run));
    run.clear();
  };
  for (const ImageId k : children) {
    if (descend(gap(k))) {
      run.push_back(k);
    } else {
      flush();
      out.push_back(k);
    }
  }
  flush();
  return out;
}

bool Lowerer::op_builds_node(ImageId op) const {
  for (std::vector todo{op}; !todo.empty();) {
    const ImageNode& n = at(todo.back());
    todo.pop_back();
    if (n.kind == ImageKind::ref && (node_rule(n.a) || infix_rule(n.a)))
      return true;
    if (n.kind != ImageKind::seq && n.kind != ImageKind::choice) continue;
    for (std::uint32_t i = 0; i < n.count; ++i)
      if (!is_condition_node(_im, child_at(n, i)))
        todo.push_back(child_at(n, i));
  }
  return false;
}

std::uint32_t Lowerer::op_rule_of(ImageId op) const {
  for (;;) {
    const ImageNode& n = at(op);
    if (n.kind == ImageKind::ref) return n.a;
    if (n.kind != ImageKind::seq) return kNoImage;
    std::size_t readers = 0;
    for (std::uint32_t i = 0; i < n.count; ++i) {
      if (is_condition_node(_im, child_at(n, i))) continue;
      ++readers;
      op = child_at(n, i);
    }
    if (readers != 1) return kNoImage;
  }
}

}  // namespace cursiv::engine
