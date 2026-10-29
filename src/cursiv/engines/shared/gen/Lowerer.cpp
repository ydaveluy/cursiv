#include "cursiv/engines/shared/gen/Lowerer.hpp"

#include <algorithm>
#include <functional>
#include <span>
#include <string>

#include "cursiv/core/grammar/GrammarPrinter.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/text/white_space.hpp"
#include "cursiv/engines/shared/gen/AstRegistryBuilder.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/UnaryLevelFault.hpp"
#include "cursiv/engines/shared/gen/WrittenReads.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

namespace {

template <class T>
bool has(const std::vector<T>& sorted, T v) {
  return std::binary_search(sorted.begin(), sorted.end(), v);
}

std::vector<std::uint32_t> sorted_unique(std::vector<std::uint32_t> v) {
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

}  // namespace

Lowerer::Lowerer(GrammarImage& image, const LowerDesc& desc,
                 LowerPools& pools, std::uint16_t& nterms, ByteSet& wordBytes,
                 ChainPool& writes, GrammarFault& fault, LowerPool& pool)
    : _im(image),
      _twins(image,
             [this](std::uint32_t rule) {
               return data_rule(rule) && !_im.rules[rule].foreign
                          ? _im.node(*stage(rule).body)
                          : kNoImage;
             }),
      _d(desc),
      _c(pools),
      _nterms(nterms),
      _writes(writes),
      _fault(fault),
      _pool(pool),
      _lex(*this, wordBytes, _pool.setIndex) {}

std::vector<ImageId> Lowerer::kids_of(const ImageNode& n) const {
  return {_im.kids.begin() + n.first, _im.kids.begin() + n.first + n.count};
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

const ImageStageDesc& Lowerer::stage(std::uint32_t rule) const {
  return _im.stage(rule);
}

bool Lowerer::rule_is(std::uint32_t rule, ImageRuleKind kind) const {
  const ImageStageDesc* const st = _im.rules[rule].desc->stage;
  return st != nullptr && st->kind == kind;
}

bool Lowerer::terminal_rule(std::uint32_t rule) const {
  return rule_is(rule, ImageRuleKind::terminal);
}

bool Lowerer::data_rule(std::uint32_t rule) const {
  return rule_is(rule, ImageRuleKind::data);
}

bool Lowerer::node_rule(std::uint32_t rule) const {
  return rule_is(rule, ImageRuleKind::node);
}

bool Lowerer::infix_rule(std::uint32_t rule) const {
  return rule_is(rule, ImageRuleKind::infix);
}

std::uint8_t Lowerer::role_in(unsigned cfg, std::uint32_t rule) const {
  if (has(_cfgs[cfg].ignore, rule)) return 1;
  if (has(_cfgs[cfg].hide, rule)) return 2;
  return 0;
}

std::uint16_t Lowerer::role_sym(std::size_t at) const {
  return static_cast<std::uint16_t>(1 + _d.terms.size() + at);
}

std::uint16_t Lowerer::sym_of(unsigned cfg, std::uint32_t rule) {
  if (const auto t = _termIdx.find(rule); t != _termIdx.end()) {
    const std::uint8_t role = role_in(cfg, rule);
    if (role != role_in(0, rule)) {
      const auto at = std::find(
          _roles.begin(), _roles.end(),
          std::pair<std::uint32_t, std::uint8_t>{rule, role});
      return role_sym(static_cast<std::size_t>(at - _roles.begin()));
    }
    return static_cast<std::uint16_t>(1 + t->second);
  }
  if (const auto n = _ntIdx.find(rule); n != _ntIdx.end())
    return nt_sym(
        static_cast<std::uint16_t>(1 + n->second + cfg * _d.nts.size()));
  refuse_lowering("the lowering names a rule it did not number");
}

std::uint16_t Lowerer::name_of(std::uint32_t rule, std::uint16_t& len) {
  const std::string_view nm = _im.rules[rule].name;
  len = static_cast<std::uint16_t>(nm.size());
  return intern_name(nm);
}

ImageId Lowerer::body_in(unsigned cfg, std::uint32_t rule) {
  const bool section = section_cfg(cfg);
  const std::uint64_t k = std::uint64_t{rule} << 1 | (section ? 1u : 0u);
  if (const auto it = _bodies.find(k); it != _bodies.end()) return it->second;
  ImageId out = _im.node(*stage(rule).body);
  if (data_rule(rule) || node_rule(rule)) out = _twins.drop(out);
  if (section) out = ended(out);
  _bodies.emplace(k, out);
  return out;
}

bool Lowerer::has_rows(ImageId id) const {
  switch (at(id).kind) {
    case ImageKind::create:
    case ImageKind::nest:
    case ImageKind::assign:
    case ImageKind::append:
    case ImageKind::flag:
      return true;
    default:
      return std::ranges::any_of(_im.shape_kids(id),
                                 [&](ImageId k) { return has_rows(k); });
  }
}

bool Lowerer::creates(ImageId id, const void* row) const {
  const ImageNode& n = at(id);
  if (n.kind == ImageKind::create && n.row == row) return true;
  for (const ImageId k : _im.shape_kids(id))
    if (creates(k, row)) return true;
  return false;
}

bool Lowerer::reads_under(unsigned cfg, std::uint32_t rule) const {
  const ImageRuleDesc* const home = _im.rules[rule].desc->home;
  const std::uint32_t section = _cfgs[cfg].section;
  if (home == nullptr) return section == kNoImage;
  return section != kNoImage && _im.rules[section].desc == home;
}

unsigned Lowerer::region_of(unsigned cfg, const ImageNode& skip) const {
  const auto lists = _im.ruleLists.begin() + skip.a;
  const auto ignore =
      sorted_unique(std::vector<std::uint32_t>(lists, lists + skip.m));
  const auto hide = sorted_unique(
      std::vector<std::uint32_t>(lists + skip.m, lists + skip.m + skip.n));
  const Config& home = _cfgs[cfg];
  for (unsigned k = 0; k < _cfgs.size(); ++k)
    if (_cfgs[k].section == home.section && _cfgs[k].end == home.end &&
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
  const ImageNode& n = at(id);
  switch (n.kind) {
    case ImageKind::lit:
      return id;
    case ImageKind::ref:
      return terminal_rule(n.a) ? id : kNoImage;
    case ImageKind::doc:
    case ImageKind::assign:
    case ImageKind::append:
      return token_core(kid(n, 0));
    default:
      return kNoImage;
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
          "tokens the lexer skips, never rules the parser reads");
  for (const Config& c : _cfgs)
    if (std::ranges::any_of(c.ignore,
                            [&](std::uint32_t r) { return has(c.hide, r); }))
      refuse_lowering(
          "a terminal belongs to at most one of `ignored` and `hidden`: "
          "trivia are dropped or kept in the tree, never both");
  for (const std::uint8_t role : {0, 1, 2})
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

  std::function<void(ImageId, unsigned)> walk;
  const auto owned = [&](unsigned home) {
    for (const std::uint32_t rule : _ntRule) {
      if (!reads_under(home, rule)) continue;
      const ImageStageDesc& st = stage(rule);
      if (st.kind != ImageRuleKind::infix) {
        walk(_im.node(*st.body), home);
        continue;
      }
      for (std::uint32_t i = 0; i < st.levelCount; ++i)
        for (std::uint32_t j = 0; j < st.levels[i].count; ++j)
          walk(_im.node(*st.levels[i].ops[j].op), home);
    }
  };
  walk = [&](ImageId id, unsigned home) {
    const ImageNode n = at(id);
    if (n.kind == ImageKind::skip) {
      const auto ignore = listed(n.a, n.m), hide = listed(n.a + n.m, n.n);
      Config c;
      c.ignore = sorted_unique(ignore);
      c.hide = sorted_unique(hide);
      if (std::ranges::none_of(_cfgs, [&](const Config& o) {
            return o.home == home && o.ignore == c.ignore && o.hide == c.hide;
          })) {
        c.home = home;
        if (home != 0) {
          c.section = _cfgs[home].section;
          c.end = _cfgs[home].end;
        }
        push(std::move(c), ignore, hide);
      }
    } else if (n.kind == ImageKind::embed &&
               at(kid(n, 0)).kind == ImageKind::seq &&
               at(kid(n, 0)).count == 3) {
      const ImageNode inner = at(kid(n, 0));
      const ImageId end = token_core(kid(inner, 2));
      if (end == kNoImage || token_core(kid(inner, 0)) == kNoImage)
        refuse_lowering(
            "`section<G>(open, close)`: `open` and `close` are each one "
            "token of the caller, a literal, a keyword or a terminal");
      walk(kid(inner, 0), home);
      if (std::ranges::none_of(_cfgs, [&](const Config& c) {
            return c.section == n.a && c.end == end;
          })) {
        const ImageFace& face = _im.ready(n.a).face[0];
        Config c;
        c.section = n.a;
        c.end = end;
        c.bound = face.bound;
        c.home = static_cast<std::uint32_t>(_cfgs.size());
        owned(push_listed(std::move(c), listed(face.ignoreAt, face.ignoreCount),
                          listed(face.hiddenAt, face.hiddenCount)));
      }
      walk(kid(inner, 2), home);
      return;
    }
    // `owned` may grow the image: the kids are read again after each walk.
    for (std::size_t i = 0, count = _im.shape_kids(id).size(); i < count; ++i)
      walk(_im.shape_kids(id)[i], home);
  };
  owned(0);

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
bool Lowerer::list_words(ImageId id,
                         std::vector<std::string_view>& words) const {
  const ImageNode& n = at(id);
  if (n.kind == ImageKind::lit) {
    if ((n.flags & kLitGuardExpr) != 0 || white_space_only(n.text))
      return false;
    if (std::ranges::find(words, n.text) == words.end())
      words.push_back(n.text);
    return true;
  }
  if (n.kind != ImageKind::choice) return false;
  for (const ImageId k : kids_of(n))
    if (!list_words(k, words)) return false;
  return true;
}

void Lowerer::declare_term(std::uint32_t rule) {
  const ImageStageDesc& st = stage(rule);
  const ImageId bodyId = body_in(0, rule);
  const ImageNode body = at(bodyId);
  const std::uint16_t t = _nterms++;
  std::uint16_t len = 0;
  const std::uint16_t off = name_of(rule, len);
  _c.terms[t].nameOff = off;
  _c.terms[t].nameLen = len;
  if (body.kind == ImageKind::lit) spell_literal(t, body);
  // A choice of words spells none of them alone, but each is a spelling.
  std::vector<std::string_view> words;
  if (body.kind == ImageKind::choice && list_words(bodyId, words))
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
  if (body.kind == ImageKind::indent) _c.terms[t].laid = kLaidIndent;
  if (body.kind == ImageKind::dedent) _c.terms[t].laid = kLaidDedent;
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
      shortened |= role_in(cf.home, rule) != 0;
  }
  _c.cfgHome[k] = static_cast<std::uint8_t>(cf.home);
  _c.cfgBare[k] = k != cf.home && cf.ignore.empty() && cf.hide.empty();
  _c.cfgShort[k] = k != cf.home && shortened;
}

void Lowerer::delimit_spelled(ImageId spelled, std::int8_t d) {
  const ImageId want = _lex.canon(spelled);
  for (const std::uint32_t rule : _termRule)
    if (_lex.canon(body_in(0, rule)) == want &&
        !_c.terms[sym_of(0, rule)].trivia)
      _c.terms[sym_of(0, rule)].delim = d;
}

void Lowerer::delimit(ImageId spelled, std::int8_t d) {
  const ImageNode n = at(spelled);
  switch (n.kind) {
    case ImageKind::epsilon:
      return;
    case ImageKind::lit:
      if (const auto t = _pool.termIndex.find(
              key(Key::token, 0, _lex.token_of(0, spelled, _d.bound)));
          t != _pool.termIndex.end())
        _c.terms[t->second].delim = d;
      delimit_spelled(spelled, d);
      return;
    case ImageKind::ref: {
      if (!_termIdx.contains(n.a))
        refuse_lowering(
            "`indent_open` and `indent_close` name a terminal a rule "
            "reads, not a rule or a terminal no rule reaches");
      const ImageId body = body_in(0, n.a);
      if (at(body).kind == ImageKind::lit)
        delimit(body, d);
      else if (!_c.terms[sym_of(0, n.a)].trivia)
        _c.terms[sym_of(0, n.a)].delim = d;
      return;
    }
    case ImageKind::choice:
      for (const ImageId k : kids_of(n)) delimit(k, d);
      return;
    default:
      refuse_lowering(
          "`indent_open` and `indent_close` name spellings or "
          "terminals: `lit<\"(\">`, `ref<LPAR>` or a choice of them");
  }
}

void Lowerer::declare() {
  if (_d.terms.size() + _roles.size() >= kNtBase ||
      _d.nts.size() * _cfgs.size() >= kNtBase)
    refuse_lowering("cursiv: declared symbols exceed their 15-bit domain");
  if (_cfgs.size() > 255) lower_refuse(_c, LowerFail::tooManyConfigurations);
  _nterms = 1;
  const std::uint16_t endName = intern_name("$end");
  _c.terms[0].nameOff = endName;
  _c.terms[0].nameLen = 4;
  declare_start();
  for (unsigned k = 0; k < _cfgs.size(); ++k)
    if (_cfgs[k].section != kNoImage && _cfgs[k].home == k)
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
          "`indent_newline` names the terminal that ends a line: a "
          "terminal a rule reads, not a rule or a terminal no rule "
          "reaches");
    _c.terms[sym_of(0, newline)].joins = true;
  }
  declare_rules();
}

bool Lowerer::clone_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs) {
  if (!reads_under(cfg, rule)) return false;
  if (!_c.cloneNeeded[lhs] || _c.cloneDone[lhs]) return false;
  _c.cloneDone[lhs] = true;
  define_one(cfg, rule, lhs, 0);
  return true;
}

bool Lowerer::buries(ImageId id) const {
  if (at(id).kind == ImageKind::nest) return true;
  for (const ImageId k : _im.shape_kids(id))
    if (buries(k)) return true;
  return false;
}

// An unordered item the text may leave out: `option(x)`, `many(x)` or
// `repeat<0, n>(x)`, seen through regions only. A region writes nothing when
// its body is skipped; a directive may still write over an empty match -- a
// `flag` sets its member, an `assign` stores the node its operand builds --
// so an item under one is read as written. The repetition, or `kNoImage`.
ImageId Lowerer::optional_item(ImageId item) const {
  while (at(item).kind == ImageKind::skip) item = kid(at(item), 0);
  const ImageNode& n = at(item);
  return n.kind == ImageKind::repeat && n.m == 0 && n.n != 0 ? item : kNoImage;
}

// An unordered item reads input, or is left out whole: one that may read
// nothing could stand anywhere in the order, a reading of one text for each
// place. An optional item is read in its present form, `x` for `option(x)`,
// which must read input, and only in a group that holds no `nest`: such a
// group spells every order in full. The rules are read as the pipeline
// resolved them, before either engine lays a body; the first item refused is
// named under the rule that holds it.
void Lowerer::refuse_empty_items() {
  if (_fault) return;
  WrittenReads reads(_im);
  std::vector<ImageId> todo;
  for (std::size_t i = 0; i < _ntRule.size(); ++i) {
    const ImageFace face = _im.ready(_ntRule[i]).face[1];
    if (face.kind != ImageRuleKind::data && face.kind != ImageRuleKind::node)
      continue;
    todo.assign(1, face.body);
    while (!todo.empty()) {
      const ImageId id = todo.back();
      todo.pop_back();
      const std::span<const ImageId> kids = _im.shape_kids(id);
      todo.insert(todo.end(), kids.rbegin(), kids.rend());
      // An answer may lay the faces of the rules it reaches: read by index.
      const ImageNode n = at(id);
      if (n.kind != ImageKind::unordered) continue;
      const bool nests = buries(id);
      for (std::uint32_t k = 0; k < n.count; ++k) {
        const ImageId item = kid(n, k);
        const ImageId optional = optional_item(item);
        const bool present =
            optional != kNoImage && !reads.nothing(kid(at(optional), 0));
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
}

void Lowerer::define() {
  refuse_empty_items();
  if (!_fault) _fault = unary_level_fault(_im, _ntRule);
  std::uint16_t lhs = 0;
  for (const std::uint32_t rule : _ntRule) {
    ++lhs;
    if (reads_under(0, rule)) define_one(0, rule, lhs, 0);
  }
  const auto n = static_cast<std::uint16_t>(_ntRule.size());
  for (bool grew = _cfgs.size() > 1; grew;) {
    grew = false;
    for (unsigned k = 1; k < _cfgs.size(); ++k) {
      auto i = static_cast<std::uint16_t>(k * n);
      for (const std::uint32_t rule : _ntRule) {
        ++i;
        grew = clone_one(k, rule, i) || grew;
      }
    }
    grew = define_open() || grew;
  }
  // The brackets once the rules numbered what they read: a spelling no rule
  // reads is no terminal of its own.
  delimit(_im.node(*_d.indentOpen), 1);
  delimit(_im.node(*_d.indentClose), -1);
  for (std::size_t i = 0; i < _roles.size(); ++i) inherit_role_term(i);
}

void Lowerer::unspell(std::uint16_t t, std::uint8_t laid) {
  _c.terms[t].spelled = false;
  _c.terms[t].textLen = 0;
  _c.terms[t].laid = laid;
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
  const std::uint16_t node = lex();
  TermBuild& build = _c.termBuild[t];
  build.lex = node;
  build.folded = folded;
  build.charset = charset;
  _c.terms[t].spelled = true;
  return t;
}

std::uint16_t Lowerer::term_of(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::lit: {
      const ImageId token = _lex.token_of(cfg, e, _d.bound);
      return intern_spelled(
          key(Key::token, 0, token), n.text, false,
          (n.flags & kLitFolded) != 0, [&] {
            return _lex.reify(_lex.resolve(cfg, e));
          });
    }
    case ImageKind::cpset: {
      const bool complement = (n.flags & kCpNot) != 0;
      const bool plain = n.m == 0 && !complement && detail::cp_ascii_of(n.text);
      std::string name;
      if (plain) {
        name = std::string(n.text);
      } else {
        detail::cp_name_emit(n.m, complement, n.text,
                             [&name](char c) { name += c; });
      }
      return intern_spelled(key(Key::token, 0, e), name, true, false,
                            [&] { return _lex.reify(e); });
    }
    case ImageKind::ref:
      return sym_of(cfg, n.a);
    case ImageKind::doc:
      return term_of(cfg, kid(n, 0));
    default:
      refuse_lowering("this expression is no single token");
  }
}

std::uint16_t Lowerer::lay_laid(ImageId e, std::uint8_t laid,
                                std::string_view name) {
  std::uint16_t t = 1;
  while (t < _nterms && _c.terms[t].laid != laid) ++t;
  if (t == _nterms) {
    t = intern_spelled(key(Key::token, 0, e), name, false, false,
                       [&] { return _lex.reify(e); });
    unspell(t, laid);
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
      if (at(kid(n, 0)).kind != ImageKind::lit)
        refuse_lowering(
            "a doc stands on a WORD a reader can hover -- `kw<\"if\">` or "
            "`lit<\"if\">`. Nothing else holds one: only a terminal has a "
            "column for it, and prose over a rule reference would have "
            "nowhere to land");
      const std::uint16_t sym = terminal_sym(cfg, kid(n, 0));
      std::string_view& doc = _c.termBuild[sym].doc;
      if (doc.empty())
        doc = n.text;
      else if (doc != n.text)
        _fault.raise(GrammarFaultCode::docConflict, _pool.owner);
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
      return lay_laid(e, kLaidIndent, "indent");
    case ImageKind::dedent:
      return lay_laid(e, kLaidDedent, "dedent");
    default:
      refuse_lowering(
          "this combinator has no lowering: a body reaching here would "
          "become a right-hand side nobody wrote");
  }
}

std::vector<ImageId> Lowerer::gap_runs(const std::vector<ImageId>& kids) {
  std::vector<ImageId> out;
  std::vector<ImageId> run;
  const auto flush = [&] {
    if (run.size() == 1)
      out.push_back(run.front());
    else if (run.size() > 1)
      out.push_back(_lex.make(ImageKind::seq, run));
    run.clear();
  };
  for (const ImageId k : kids) {
    if (gap(k)) {
      run.push_back(k);
    } else {
      flush();
      out.push_back(k);
    }
  }
  flush();
  return out;
}

std::uint32_t Lowerer::op_rule_of(ImageId op) const {
  const ImageNode& n = at(op);
  if (n.kind == ImageKind::ref) return n.a;
  if (n.kind != ImageKind::seq) return kNoImage;
  ImageId reader = kNoImage;
  std::size_t readers = 0;
  for (std::uint32_t i = 0; i < n.count; ++i) {
    if (is_condition_node(_im, kid(n, i))) continue;
    ++readers;
    reader = kid(n, i);
  }
  return readers == 1 ? op_rule_of(reader) : kNoImage;
}

}  // namespace cursiv
