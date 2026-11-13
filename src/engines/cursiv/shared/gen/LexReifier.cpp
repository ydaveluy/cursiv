#include "cursiv/shared/gen/LexReifier.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/grammar/expr.hpp"
#include "cursiv/grammar/word_final_in.hpp"
#include "cursiv/shared/gen/LexCodePoints.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/Lowerer.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/utils/ClassSpelling.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/GrammarPrinter.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/CaseFold.hpp"

namespace cursiv::engine {

namespace {

ByteSet byte_range(unsigned lo, unsigned hi) {
  ByteSet s;
  s.add(lo, hi);
  return s;
}

// The cases of one letter of a case-insensitive literal that share every
// byte but the last: those bytes, and the set the last one reads in.
struct CaseGroup {
  std::string head;
  ByteSet last;
};

// A letter's cases by group, its own first. An ASCII letter is one group of
// two bytes; `é` and `É` are one group, `C3` then `A9` or `89`; the cases of
// `σ` start on two bytes, so they are two groups.
std::vector<CaseGroup> case_groups(char32_t cp) {
  std::vector cases{cp};
  for_each_other_case(cp, [&](char32_t other) { cases.push_back(other); });
  std::vector<CaseGroup> out;
  for (const char32_t c : cases) {
    std::string bytes;
    append_scalar(bytes, c);
    const auto last = static_cast<unsigned char>(bytes.back());
    bytes.pop_back();
    auto at = std::ranges::find(out, bytes, &CaseGroup::head);
    if (at == out.end()) at = out.insert(out.end(), CaseGroup{bytes, {}});
    at->last.add(last);
  }
  return out;
}

// Reads literal `n` unit by unit: `byte` gets the set each byte reads in, and
// `choice` the cases of a folded letter that differ before their last byte.
template <class Byte, class Choice>
void lit_units(const ImageNode& n, Byte&& byte, Choice&& choice) {
  const bool folded = (n.flags & kLitFolded) != 0;
  for (std::size_t i = 0; i < n.text.size();) {
    std::size_t len = 0;
    const char32_t cp =
        folded ? scalar_at(n.text.data() + i, n.text.size() - i, len) : 0;
    if (len == 0) {
      const auto b = static_cast<unsigned char>(n.text[i++]);
      byte(byte_range(b, b));
      continue;
    }
    i += len;
    const std::vector<CaseGroup> groups = case_groups(cp);
    if (groups.size() > 1) {
      choice(groups);
      continue;
    }
    for (const char b : groups.front().head)
      byte(byte_range(static_cast<unsigned char>(b),
                      static_cast<unsigned char>(b)));
    byte(groups.front().last);
  }
}

// Whether literal `n` reads a letter through a choice of its cases.
bool lit_chooses(const ImageNode& n) {
  bool chooses = false;
  lit_units(n, [](const ByteSet&) {}, [&](const auto&) { chooses = true; });
  return chooses;
}

ByteSet class_bytes(std::string_view spec) {
  return bytes_of(parse_charset(spec));
}

LexCodePoints cps_of(const ImageNode& n) {
  return LexCodePoints::of(n.m, (n.flags & kCpNot) != 0, n.text);
}

bool ascii_class(const ImageNode& n) { return n.m == 0 && cp_ascii_of(n.text); }

bool one_character(std::string_view s) {
  std::size_t len = 0;
  const char32_t cp = scalar_at(s.data(), s.size(), len);
  return s.size() == 1 || (cp != kNoScalar && len == s.size());
}

}  // namespace

LexReifier::LexReifier(Lowerer& lowerer, ByteSet& wordBytes)
    : _lowerer(lowerer),
      _im(lowerer._im),
      _c(lowerer._c),
      _wordBytes(wordBytes) {}

std::uint16_t LexReifier::intern(const ByteSet& set, std::uint16_t decode) {
  return lex_intern_set(_c, set, decode);
}

std::uint16_t LexReifier::group(LexKind kind,
                                std::span<const std::uint16_t> children,
                                std::uint32_t min) {
  const std::uint16_t base = _c.nlexChild;
  for (const std::uint16_t k : children) push_child(k);
  return push(
      LexNode{kind, base, static_cast<std::uint16_t>(children.size()), min, 0});
}

std::uint16_t LexReifier::refusal(std::uint16_t bound,
                                  std::span<const std::uint16_t> classes,
                                  std::uint32_t scalar) {
  const std::uint16_t base = _c.nlexChild;
  for (const std::uint16_t c : classes) push_child(c);
  return push(LexNode{LexKind::nguard, bound, base,
                      static_cast<std::uint32_t>(classes.size()), scalar});
}

ImageId LexReifier::make(ImageKind kind, std::span<const ImageId> children,
                         std::uint8_t flags, std::uint32_t a, std::uint64_t m,
                         std::uint64_t n, std::string_view text) {
  const ImageNode shape{
      .kind = kind, .flags = flags, .a = a, .m = m, .n = n, .text = text};
  return _im.derive(shape, children);
}

ImageId LexReifier::make_class(const ImageClassDesc& cls) {
  return make(ImageKind::cpset, {},
              static_cast<std::uint8_t>(cls.complement ? kCpNot : 0), kNoImage,
              cls.props, 0, cls.scalars);
}

ImageId LexReifier::rebuilt(ImageId id, const std::vector<ImageId>& children) {
  if (std::ranges::equal(_im.shape_children(id), children)) return id;
  return _im.derive(ImageNode{at(id)}, children);
}

ImageId LexReifier::lit_with(const ImageNode& lit, ImageId guard) {
  ImageNode shape = lit;
  shape.flags = static_cast<std::uint8_t>(
      (lit.flags & kLitFolded) | (guard != kNoImage ? kLitGuardExpr : 0));
  const ImageId children[] = {guard};
  return _im.derive(shape, std::span(children, guard != kNoImage ? 1 : 0));
}

ImageId LexReifier::canon(ImageId id) { return descend(canon_walk(id)); }

Descent<ImageId> LexReifier::canon_walk(ImageId id) {
  if (const auto it = _canon.find(id); it != _canon.end()) co_return it->second;
  const ImageNode n = at(id);
  std::vector<ImageId> ks;
  for (std::uint32_t i = 0; i < n.count; ++i)
    ks.push_back(co_await canon_walk(child_at(n, i)));
  std::uint32_t a = n.a;
  std::string lists;
  if (n.kind == ImageKind::skip) {
    a = kNoImage;
    for (std::uint64_t i = 0; i < n.m + n.n; ++i) {
      const std::uint32_t r = _im.ruleLists[n.a + i];
      lists.append(reinterpret_cast<const char*>(&r), sizeof r);
    }
  }
  const auto at = _classes
                      .try_emplace(std::tuple(n.kind, n.flags, a, n.m, n.n,
                                              std::string(n.text), lists, n.tag,
                                              std::move(ks)),
                                   id)
                      .first;
  _canon.emplace(id, at->second);
  co_return at->second;
}

ImageId LexReifier::resolve(unsigned cfg, ImageId id,
                            std::span<const std::uint32_t> path) {
  _resolving.insert(path.begin(), path.end());
  const ImageId out = descend(resolve_in(cfg, id));
  for (const std::uint32_t rule : path) _resolving.erase(rule);
  return out;
}

Descent<ImageId> LexReifier::resolve_rule(unsigned cfg, std::uint32_t rule) {
  const auto call = [&] {
    const auto at = _lowerer._termIdx.find(rule);
    const std::uint32_t term =
        at == _lowerer._termIdx.end() ? kNoImage : at->second;
    if (term == kNoImage)
      refuse_lowering("cursiv: a lexical call names an unreached terminal");
    return make(ImageKind::call, {}, 0, kNoImage, 1u + term);
  };
  if (_resolving.contains(rule)) co_return call();
  const ImageId body = terminal_body(rule, _lowerer.body_in(cfg, rule));
  _resolving.insert(rule);
  const ImageId resolved = co_await resolve_in(cfg, body);
  _resolving.erase(rule);
  if (co_await has_captures(resolved)) co_return call();
  co_return resolved;
}

Descent<ImageId> LexReifier::resolve_in(unsigned cfg, ImageId id) {
  switch (const ImageNode n = at(id); n.kind) {
    case ImageKind::ref:
      if (n.a != kNoImage && _lowerer.terminal_rule(n.a))
        co_return co_await resolve_rule(cfg, n.a);
      co_return id;
    case ImageKind::lit: {
      if ((n.flags & kLitGuardExpr) == 0) co_return id;
      const ImageId g = child_at(n, 0);
      const ImageId r = co_await resolve_in(cfg, g);
      co_return r == g ? id : lit_with(n, r);
    }
    case ImageKind::notOf:
      if (at(child_at(n, 0)).kind == ImageKind::byte)
        co_return make(ImageKind::eof);
      break;
    case ImageKind::seq:
      co_return co_await resolve_seq(cfg, id);
    case ImageKind::sectionClose: {
      const ImageId end = _lowerer._cfgs[cfg].end;
      if (end == kNoImage)
        refuse_lowering("cursiv: a section's close is read outside any section");
      co_return co_await resolve_in(cfg, end);
    }
    default:
      break;
  }
  const auto shape = _im.shape_children(id);
  std::vector<ImageId> ks(shape.begin(), shape.end());
  for (ImageId& k : ks) k = co_await resolve_in(cfg, k);
  co_return rebuilt(id, ks);
}

Descent<bool> LexReifier::consumes(ImageId id) const {
  const ImageNode& n = at(id);
  switch (n.kind) {
    case ImageKind::lit:
    case ImageKind::cpset:
    case ImageKind::byte:
      co_return true;
    case ImageKind::seq:
      for (const ImageId k : _lowerer.children_of(n))
        if (co_await consumes(k)) co_return true;
      co_return false;
    case ImageKind::choice:
      for (const ImageId k : _lowerer.children_of(n))
        if (!co_await consumes(k)) co_return false;
      co_return true;
    case ImageKind::repeat:
      if (n.m == 0) co_return false;
      co_return co_await consumes(child_at(n, 0));
    default:
      co_return false;
  }
}

bool LexReifier::eof_lit(ImageId id) const {
  const ImageNode& n = at(id);
  if (n.kind != ImageKind::lit || (n.flags & kLitGuardExpr) == 0) return false;
  const ImageNode& g = at(child_at(n, 0));
  if (g.kind != ImageKind::notOf) return false;
  const ImageNode& k = at(child_at(g, 0));
  if (k.kind == ImageKind::eof) return true;
  return k.kind == ImageKind::choice &&
         std::ranges::any_of(_lowerer.children_of(k), [&](ImageId c) {
           return at(c).kind == ImageKind::eof;
         });
}

ImageId LexReifier::without_eof(ImageId id) {
  const ImageNode n = at(id);
  if ((n.flags & kLitGuardExpr) == 0) return id;
  const ImageNode g = at(child_at(n, 0));
  if (g.kind != ImageKind::notOf) return id;
  const ImageNode k = at(child_at(g, 0));
  if (k.kind == ImageKind::eof) return lit_with(n, kNoImage);
  if (k.kind != ImageKind::choice) return id;
  std::vector<ImageId> rest;
  for (const ImageId l : _lowerer.children_of(k))
    if (at(l).kind != ImageKind::eof) rest.push_back(l);
  ImageId guard = kNoImage;
  if (rest.size() == 1)
    guard = make_not(rest.front());
  else if (rest.size() > 1)
    guard = make_not(make(ImageKind::choice, rest));
  if (guard != kNoImage && canon(guard) == canon(child_at(n, 0))) return id;
  return lit_with(n, guard);
}

Descent<ImageId> LexReifier::resolve_seq(unsigned cfg, ImageId id) {
  const ImageNode n = at(id);
  std::vector<ImageId> es;
  for (const ImageId k : _lowerer.children_of(n))
    es.push_back(co_await resolve_in(cfg, k));
  if (std::ranges::any_of(es, [&](ImageId e) { return eof_lit(e); })) {
    // The literals before the last part that consumes drop their eof refusal.
    std::size_t last = es.size();
    for (; last > 0; --last)
      if (co_await consumes(es[last - 1])) break;
    for (std::size_t i = 0; i + 1 < last; ++i)
      if (at(es[i]).kind == ImageKind::lit) es[i] = without_eof(es[i]);
  }
  co_return rebuilt(id, es);
}

Descent<bool> LexReifier::has_captures(ImageId id) const {
  const ImageNode& n = at(id);
  switch (n.kind) {
    case ImageKind::mark:
    case ImageKind::same:
      co_return true;
    case ImageKind::lit:
      if ((n.flags & kLitGuardExpr) == 0) co_return false;
      co_return co_await has_captures(child_at(n, 0));
    default:
      for (const ImageId k : _im.shape_children(id))
        if (co_await has_captures(k)) co_return true;
      co_return false;
  }
}

LexReifier::EndCloser LexReifier::end_closer(ImageId close) {
  const ImageNode n = at(close);
  if (n.kind != ImageKind::choice) return {close, false};
  const std::vector<ImageId> ks = _lowerer.children_of(n);
  if (ks.size() < 2 || at(ks.back()).kind != ImageKind::eof)
    return {close, false};
  if (ks.size() == 2) return {ks[0], true};
  std::vector<ImageId> joined;
  for (std::size_t i = 0; i + 1 < ks.size(); ++i) {
    if (at(ks[i]).kind == ImageKind::choice)
      for (const ImageId k : _lowerer.children_of(at(ks[i])))
        joined.push_back(k);
    else
      joined.push_back(ks[i]);
  }
  return {make(ImageKind::choice, joined), true};
}

// The plain literals `id` spells, one or a choice of them, into `out`; false
// otherwise, or where one starts with a UTF-8 continuation byte, which the
// native search could find inside a character.
bool LexReifier::nested_literals(ImageId id,
                                 std::vector<std::string_view>& out) const {
  for (std::vector todo{id}; !todo.empty();) {
    const ImageNode& n = at(todo.back());
    todo.pop_back();
    if (n.kind == ImageKind::lit) {
      if (n.flags != 0 ||
          (static_cast<unsigned char>(n.text[0]) & 0xC0u) == 0x80u)
        return false;
      out.push_back(n.text);
      continue;
    }
    if (n.kind != ImageKind::choice || n.count < 2) return false;
    for (std::uint32_t i = n.count; i-- > 0;) todo.push_back(child_at(n, i));
  }
  return true;
}

bool LexReifier::nested_unambiguous(ImageId open, ImageId close) {
  std::vector<std::string_view> all;
  if (!nested_literals(open, all)) return false;
  const std::size_t openCount = all.size();
  if (!nested_literals(end_closer(close).type, all)) return false;
  const std::size_t closeCount = all.size() - openCount;
  const std::size_t most = openCount > 1 || closeCount > 1 ? 255u : 65535u;
  for (const std::string_view s : all)
    if (s.size() > most) return false;
  std::ranges::sort(all);
  for (std::size_t i = 1; i < all.size(); ++i)
    if (all[i].starts_with(all[i - 1])) return false;
  return true;
}

bool LexReifier::nested_item(std::uint32_t rule, ImageId open, ImageId close,
                             ImageId item) {
  const ImageId endClose = end_closer(close).type;
  const auto same = [&](ImageId a, ImageId b) { return canon(a) == canon(b); };
  const auto closes = [&](ImageId a) {
    return same(a, close) || same(a, endClose);
  };
  const auto calls = [&](ImageId r) {
    const ImageNode& n = at(r);
    return n.kind == ImageKind::ref && n.a == rule;
  };
  const auto refuses = [&](ImageId r, ImageId& a) {
    const ImageNode& n = at(r);
    if (n.kind != ImageKind::notOf) return false;
    a = child_at(n, 0);
    return true;
  };
  const auto any_char = [&](ImageId r) {
    const ImageNode& n = at(r);
    return n.kind == ImageKind::cpset && n.m == 0 && (n.flags & kCpNot) != 0 &&
           n.text.empty();
  };
  const auto both = [&](ImageId seq) {
    const ImageNode& n = at(seq);
    ImageId a = kNoImage;
    ImageId b = kNoImage;
    if (n.kind != ImageKind::seq || n.count != 3 ||
        !refuses(child_at(n, 0), a) || !refuses(child_at(n, 1), b) ||
        !any_char(child_at(n, 2)))
      return false;
    return (same(a, open) && closes(b)) || (same(b, open) && closes(a));
  };
  const ImageNode& n = at(item);
  if (n.kind == ImageKind::choice && n.count == 2) {
    const ImageId x = child_at(n, 0);
    const ImageId y = child_at(n, 1);
    if (calls(x)) return both(y);
    if (calls(y)) return both(x);
    return false;
  }
  if (n.kind == ImageKind::seq && n.count == 2) {
    ImageId a = kNoImage;
    if (!refuses(child_at(n, 0), a)) return false;
    const ImageNode& c = at(child_at(n, 1));
    if (c.kind != ImageKind::choice || c.count != 2 || !calls(child_at(c, 0)))
      return false;
    const ImageNode& s = at(child_at(c, 1));
    if (ImageId o = kNoImage; s.kind != ImageKind::seq || s.count != 2 ||
                              !refuses(child_at(s, 0), o) || !same(o, open) ||
                              !any_char(child_at(s, 1)))
      return false;
    return closes(a);
  }
  return false;
}

ImageId LexReifier::terminal_body(std::uint32_t rule, ImageId body) {
  const ImageNode& b = at(body);
  if (b.kind != ImageKind::seq || b.count != 3) return body;
  const ImageNode& rep = at(child_at(b, 1));
  if (rep.kind != ImageKind::repeat || rep.m != 0 || rep.n != kUnbounded)
    return body;
  const ImageId open = child_at(b, 0);
  const ImageId close = child_at(b, 2);
  if (!nested_item(rule, open, close, child_at(rep, 0)) ||
      !nested_unambiguous(open, close))
    return body;
  const ImageId ends[] = {open, close};
  return make(ImageKind::throughNested, ends);
}

// The key of a literal's token. A keyword's is the literal refusing its word
// boundary, complement included.
ImageId LexReifier::token_of(unsigned cfg, ImageId id,
                             const ImageClassDesc& bound) {
  const ImageNode n = at(id);
  if (n.flags & kLitGuardWord) {
    const bool ends = word_final_in(n.text, bound.props, bound.complement,
                                    bound.scalars, (n.flags & kLitFolded) != 0);
    return lit_with(n, ends ? make_not(make_class(bound)) : kNoImage);
  }
  if (n.flags & kLitGuardExpr) return lit_with(n, resolve(cfg, child_at(n, 0)));
  return id;
}

void LexReifier::declare_word(const ImageClassDesc& bound) {
  const LexCodePoints cps =
      LexCodePoints::of(bound.props, bound.complement, bound.scalars);
  _wordBytes = cps.bytes();
  LexCodePoints wide;
  wide.ranges = cps.wide();
  for (const unicode::Range& r : wide.ranges)
    _c.wordRanges[_c.wordRanges.size()] = r;
  _c.wordLeads = wide.leads();
}

void LexReifier::declare_section_word(const ImageClassDesc& bound) {
  const LexCodePoints cps =
      LexCodePoints::of(bound.props, bound.complement, bound.scalars);
  if (_wordBytes == cps.bytes() && _c.wordRanges.v == cps.wide()) return;
  _c.note_section_word(
      {cps.bytes(), cps.wide(),
       cps.wide().empty() ? intern(cps.bytes())
                          : lex_intern_cps_bound(_c, cps.bytes(), cps)});
}

bool LexReifier::refusable(ImageId id) const {
  const ImageNode& n = at(id);
  if (n.kind == ImageKind::cpset)
    return (n.flags & kCpNot) == 0 || ascii_class(n);
  if (n.kind == ImageKind::lit) {
    const Guard g = guard_of(n);
    return !g.expression && !g.words;
  }
  return false;
}

LexReifier::Guard LexReifier::classify(ImageId id) const {
  Guard out;
  out.node = id;
  const ImageNode& n = at(id);
  if (n.kind != ImageKind::notOf) {
    out.expression = true;
    return out;
  }
  const ImageId k = child_at(n, 0);
  const ImageNode& kn = at(k);
  if (kn.kind == ImageKind::lit) {
    const Guard inner = guard_of(kn);
    out.expression = inner.expression || inner.words;
    out.words = !out.expression;
    if (out.words) out.refused = {k};
    return out;
  }
  if (kn.kind == ImageKind::cpset) {
    out.cp = k;
    if (ascii_class(kn)) {
      out.single = true;
      out.bytes = class_bytes(kn.text);
      if (kn.flags & kCpNot) out.bytes = lex_not(out.bytes);
    } else if (kn.flags & kCpNot) {
      out.expression = true;
    } else {
      out.words = true;
      out.refused = {k};
    }
    return out;
  }
  if (kn.kind == ImageKind::choice) {
    const std::vector<ImageId> ks = _lowerer.children_of(kn);
    if (std::ranges::all_of(ks, [&](ImageId e) { return refusable(e); })) {
      out.words = true;
      out.refused = ks;
      return out;
    }
  }
  out.expression = true;
  return out;
}

LexReifier::Guard LexReifier::guard_of(const ImageNode& lit) const {
  if (lit.flags & kLitGuardWord) {
    Guard g;
    g.word = true;
    return g;
  }
  if (lit.flags & kLitGuardExpr) return classify(child_at(lit, 0));
  return {};
}

std::uint16_t LexReifier::word_set() {
  const std::uint16_t decode =
      _c.wordRanges.size() != 0 ? intern(_c.wordLeads) : kNoSet;
  return intern(_wordBytes, decode);
}

std::uint16_t LexReifier::guard_set(const Guard& g, bool tail) {
  const bool scalars = _c.wordRanges.size() != 0;
  if (g.word) return word_set();
  if (g.words) {
    if (g.cp == kNoImage) return kNoSet;
    const LexCodePoints cps = cps_of(at(g.cp));
    const ByteSet bytes = cps.bytes();
    const auto wide = cps.wide();
    if ((tail || !scalars) && _wordBytes == bytes && _c.wordRanges.v == wide)
      return word_set();
    for (const LexWordClass& w : _c.sectionWords.v)
      if (w.bytes == bytes && w.wide == wide)
        return tail || w.wide.empty() ? w.set : kNoSet;
    return kNoSet;
  }
  if (g.single) return intern(g.bytes);
  return kNoSet;
}

std::uint16_t LexReifier::lit_guard(const ImageNode& lit, const Guard& g,
                                    bool tail) {
  const ImageClassDesc& bound = _lowerer._d.bound;
  return g.word && !word_final_in(lit.text, bound.props, bound.complement,
                                  bound.scalars, (lit.flags & kLitFolded) != 0)
             ? kNoSet
             : guard_set(g, tail);
}

std::uint16_t LexReifier::class_guard(std::uint16_t guard) {
  for (std::uint16_t i = 0; i < _c.nlex; ++i)
    if (_c.lex[i].kind == LexKind::nguard && _c.lex[i].a == kNoSet &&
        _c.lex[i].min == 1 && _c.lexChild[_c.lex[i].b] == guard)
      return i;
  return refusal(kNoSet, std::span(&guard, 1));
}

std::uint16_t LexReifier::refused(ImageId word, std::uint16_t node) {
  _c.lexSpelled[node] = GrammarPrinter(_im, ImageView::written).spell(word);
  return node;
}

void LexReifier::class_refusals(ImageId id, std::vector<std::uint16_t>& out) {
  const ImageNode n = at(id);
  const auto one = [&](const ByteSet& set) {
    const std::uint16_t cls = intern(set);
    out.push_back(refused(id, refusal(kNoSet, std::span(&cls, 1))));
  };
  if (n.flags & kCpNot) {
    one(lex_not(class_bytes(n.text)));
    return;
  }
  const LexCodePoints cps = cps_of(n);
  for (const LexCodePoints::Seq& q : cps.sequences()) {
    if (q.len == 1) continue;
    std::uint16_t cls[4];
    for (unsigned k = 0; k < q.len; ++k)
      cls[k] = intern(byte_range(q.lo[k], q.hi[k]));
    out.push_back(refused(id, refusal(kNoSet, std::span(cls, q.len))));
  }
  if (const ByteSet bytes = cps.bytes(); bytes.any()) one(bytes);
}

Descent<void> LexReifier::refusals(const Guard& g,
                                   std::vector<std::uint16_t>& out) {
  for (const ImageId w : g.refused) {
    if (at(w).kind == ImageKind::cpset) {
      class_refusals(w, out);
    } else {
      const std::uint16_t node = co_await reify_not_lit(w);
      out.push_back(node);
    }
  }
}

Descent<std::uint16_t> LexReifier::reify_lit(ImageId id, bool tail) {
  const ImageNode n = at(id);
  const Guard g = guard_of(n);
  const std::uint16_t guard = lit_guard(n, g, tail);
  const bool guarded = guard != kNoSet;
  const std::uint16_t barred = guarded ? class_guard(guard) : 0;
  std::vector<std::uint16_t> words;
  if (g.words && !guarded) co_await refusals(g, words);
  if (g.expression) {
    const std::uint16_t node = co_await reify_walk(g.node, true);
    words.push_back(push(LexNode{LexKind::positive, node}));
  }
  std::vector<std::uint16_t> children;
  const auto chars = [&](const ByteSet& set) {
    return push(LexNode{LexKind::chars, intern(set), 0, 0, 0});
  };
  lit_units(
      n, [&](const ByteSet& set) { children.push_back(chars(set)); },
      [&](const std::vector<CaseGroup>& groups) {
        std::vector<std::uint16_t> arms;
        for (const CaseGroup& c : groups) {
          std::vector<std::uint16_t> bytes;
          for (const char b : c.head)
            bytes.push_back(chars(byte_range(static_cast<unsigned char>(b),
                                             static_cast<unsigned char>(b))));
          bytes.push_back(chars(c.last));
          arms.push_back(bytes.size() == 1 ? bytes.front()
                                           : group(LexKind::seq, bytes));
        }
        children.push_back(group(LexKind::alt, arms, 1));
      });
  if (guarded) children.push_back(barred);
  children.insert(children.end(), words.begin(), words.end());
  co_return group(LexKind::seq, children, 1);
}

Descent<std::uint16_t> LexReifier::reify_not_lit(ImageId id) {
  const ImageNode n = at(id);
  const Guard g = guard_of(n);
  // A refusal reads one set per byte, which a choice of cases is not.
  if (lit_chooses(n) ||
      ((g.expression || g.words) && lit_guard(n, g, true) == kNoSet)) {
    const std::uint16_t node = co_await reify_lit(id, true);
    co_return push(LexNode{LexKind::negative, node});
  }
  std::vector<std::uint16_t> classes;
  lit_units(
      n, [&](const ByteSet& set) { classes.push_back(intern(set)); },
      [](const auto&) {});
  co_return refused(id, refusal(lit_guard(n, g, true), classes));
}

std::uint16_t LexReifier::reify_not_class(ImageId id) {
  const ImageNode n = at(id);
  if (!refusable(id)) {
    const ImageId plain =
        make(ImageKind::cpset, {}, static_cast<std::uint8_t>(n.flags & ~kCpNot),
             kNoImage, n.m, 0, n.text);
    const std::uint16_t arms[] = {reify_class(plain), reify_eof()};
    return push(LexNode{LexKind::positive, group(LexKind::alt, arms)});
  }
  std::vector<std::uint16_t> nodes;
  class_refusals(id, nodes);
  return nodes.size() == 1 ? nodes.front() : group(LexKind::seq, nodes);
}

Descent<std::uint16_t> LexReifier::reify_not_of(ImageId id) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::lit:
      co_return co_await reify_not_lit(id);
    case ImageKind::cpset:
      co_return reify_not_class(id);
    case ImageKind::choice: {
      std::vector<std::uint16_t> arms;
      for (const ImageId k : _lowerer.children_of(n))
        arms.push_back(co_await reify_not_of(k));
      co_return group(LexKind::seq, arms);
    }
    default: {
      const std::uint16_t node = co_await reify_walk(id, true);
      co_return push(LexNode{LexKind::negative, node});
    }
  }
}

std::uint16_t LexReifier::reify_eof() {
  const std::uint16_t any = intern(kAllBytes);
  return refusal(kNoSet, std::span(&any, 1));
}

std::uint16_t LexReifier::reify_class(ImageId id) {
  const ImageId key = canon(id);
  if (const auto found = _codePoints.find(key); found != _codePoints.end())
    return found->second;
  const LexCodePoints cps = cps_of(at(id));
  std::vector<std::uint16_t> alts;
  for (const LexCodePoints::Seq& q : cps.sequences()) {
    if (q.len == 1) continue;
    std::uint16_t children[4];
    for (unsigned k = 0; k < q.len; ++k)
      children[k] = push(LexNode{
          LexKind::chars, intern(byte_range(q.lo[k], q.hi[k])), 0, 0, 0});
    alts.push_back(group(LexKind::seq, std::span(children, q.len)));
  }
  if (const ByteSet bytes = cps.bytes(); bytes.any())
    alts.push_back(push(LexNode{LexKind::chars, intern(bytes), 0, 0, 0}));
  if (cps.holds_invalid()) {
    for (const LexCodePoints::LoneLead& lead : LexCodePoints::lone_leads()) {
      const std::uint16_t head =
          push(LexNode{LexKind::chars, intern(lead.lead), 0, 0, 0});
      std::uint16_t rest[3];
      for (unsigned k = 0; k < lead.len; ++k) rest[k] = intern(lead.rest[k]);
      const std::uint16_t parts[] = {
          head, refusal(kNoSet, std::span(rest, lead.len), 1)};
      alts.push_back(group(LexKind::seq, parts));
    }
  }
  const std::uint16_t root =
      alts.size() == 1 ? alts.front() : group(LexKind::alt, alts);
  _codePoints.emplace(key, root);
  return root;
}

bool LexReifier::mark_run(ImageId id) const {
  const ImageNode& n = at(id);
  const ImageNode& k = n.kind == ImageKind::repeat ? at(child_at(n, 0)) : n;
  if (k.kind == ImageKind::cpset) return true;
  return k.kind == ImageKind::lit && k.flags == 0 && one_character(k.text);
}

Descent<std::uint16_t> LexReifier::reify_mark(ImageId id, bool tail) {
  const ImageNode n = at(id);
  if (!mark_run(child_at(n, 0)))
    refuse_lowering(
        "`mark<>` takes one character class or one-character literal, "
        R"(alone or repeated: `many(lit<"=">)`, `some(w)`, )"
        R"(`repeat<0, 16>(cr<"^() ">)`)");
  const std::uint16_t childNode = co_await reify_walk(child_at(n, 0), tail);
  co_return push(LexNode{LexKind::mark, childNode, 0,
                         _c.intern_tag(*static_cast<const Tag*>(n.tag)), 0});
}

std::uint16_t LexReifier::reify_ref(ImageId id) {
  const ImageNode n = at(id);
  const std::uint16_t node = push(LexNode{LexKind::seq, _c.nlexChild, 0, 0, 0});
  if (!_c.lowerFail()) {
    _c.lexSpelled[node] = std::string(_im.rules[n.a].name);
    _c.lowerWord = node;
  }
  lower_refuse(_c, LowerFail::terminalReadsRule);
  return node;
}

bool LexReifier::echo_pad(ImageId id) const {
  const ImageNode& n = at(id);
  return n.kind == ImageKind::repeat && n.m == 0 && n.n == kUnbounded &&
         at(child_at(n, 0)).kind == ImageKind::cpset;
}

LexReifier::Echo LexReifier::echo_closer(ImageId close) const {
  const ImageNode& n = at(close);
  if (n.kind != ImageKind::seq) return {};
  const std::vector<ImageId> ks = _lowerer.children_of(n);
  const auto plain = [&](ImageId id) {
    const ImageNode& l = at(id);
    return l.kind == ImageKind::lit && (l.flags & kLitFolded) == 0;
  };
  const auto bare = [&](ImageId id) {
    return id == kNoImage ||
           (plain(id) && (at(id).flags & (kLitGuardWord | kLitGuardExpr)) == 0);
  };
  const auto spelled = [&](ImageId id) {
    return id == kNoImage ? 0 : at(id).text.size();
  };
  std::size_t i = 0;
  Echo e;
  if (i < ks.size() && plain(ks[i])) {
    e.before = ks[i++];
    if (i < ks.size() && echo_pad(ks[i])) e.pad = ks[i++];
  }
  if (i == ks.size() || at(ks[i]).kind != ImageKind::same) return {};
  e.tag = at(ks[i++]).tag;
  if (i < ks.size() && plain(ks[i])) e.after = ks[i++];
  if (i != ks.size()) return {};
  e.echoes = bare(e.before) && bare(e.after) &&
             spelled(e.before) + spelled(e.after) != 0;
  return e;
}

Descent<std::uint16_t> LexReifier::reify_through_lit(ImageId open,
                                                     std::string_view close,
                                                     bool eof) {
  const std::size_t n = close.size();
  const std::uint16_t openNode = co_await reify_walk(open, false);
  const std::uint16_t end = eof ? class_guard(intern(kAllBytes)) : kNoLexNode;

  std::vector<std::size_t> fail(n + 1, 0);
  for (std::size_t i = 1, k = 0; i < n; ++i) {
    while (k > 0 && close[i] != close[k]) k = fail[k - 1];
    if (close[i] == close[k]) ++k;
    fail[i] = k;
  }

  const std::uint16_t base = _c.nlexChild;
  std::vector<ByteSet> to(n + 1);
  for (std::size_t i = 0; i < n; ++i) {
    std::ranges::fill(to, ByteSet{});
    for (std::size_t b = 0; b < 256; ++b) {
      std::size_t k = i;
      while (k > 0 && static_cast<char>(b) != close[k]) k = fail[k - 1];
      to[(static_cast<char>(b) == close[k]) ? k + 1 : 0].add(
          static_cast<unsigned char>(b));
    }
    for (const ByteSet& bs : to) push_child(bs.any() ? intern(bs) : kNoSet);
  }
  if (end != kNoLexNode) push_child(end);
  co_return push(LexNode{LexKind::through_lit, openNode, base,
                         static_cast<std::uint32_t>(n), eof ? 1u : 0u});
}

Descent<std::uint16_t> LexReifier::echo_side(ImageId side) {
  if (side == kNoImage) co_return kNoLexNode;
  co_return co_await reify_walk(side, true);
}

Descent<std::uint16_t> LexReifier::reify_through(ImageId id, bool tail) {
  const ImageNode n = at(id);
  const ImageId open = child_at(n, 0);
  const ImageId close = child_at(n, 1);
  if (const Echo echo = echo_closer(close); echo.echoes) {
    const std::uint16_t openNode = co_await reify_walk(open, false);
    const std::uint16_t before = co_await echo_side(echo.before);
    const std::uint16_t after = co_await echo_side(echo.after);
    const std::uint16_t pad = co_await echo_side(echo.pad);
    const std::uint16_t base = _c.nlexChild;
    push_child(before);
    push_child(after);
    if (pad != kNoLexNode) push_child(pad);
    co_return push(LexNode{LexKind::through_same, openNode, base,
                           pad != kNoLexNode ? 3u : 2u,
                           _c.intern_tag(*static_cast<const Tag*>(echo.tag))});
  }
  const EndCloser end = end_closer(close);
  if (const ImageNode& closer = at(end.type);
      closer.kind == ImageKind::lit && closer.flags == 0 &&
      !closer.text.empty() &&
      (static_cast<unsigned char>(closer.text[0]) & 0xC0) != 0x80)
    co_return co_await reify_through_lit(open, closer.text, end.eof);
  const ImageId notClose = make_not(close);
  const ImageId anyChar =
      make(ImageKind::cpset, {}, static_cast<std::uint8_t>(kCpNot));
  const ImageId item[] = {notClose, anyChar};
  const ImageId itemSeq = make(ImageKind::seq, item);
  const ImageId rep = make(ImageKind::repeat, std::span(&itemSeq, 1), 0,
                           kNoImage, 0, kUnbounded);
  const ImageId parts[] = {open, rep, close};
  co_return co_await reify_walk(make(ImageKind::seq, parts), tail);
}

Descent<std::uint16_t> LexReifier::reify_nested(ImageId id) {
  const ImageNode n = at(id);
  const EndCloser end = end_closer(child_at(n, 1));
  const std::uint16_t open = co_await reify_walk(child_at(n, 0), false);
  const std::uint16_t close = co_await reify_walk(end.type, false);
  const std::uint16_t base = _c.nlexChild;
  push_child(open);
  push_child(close);
  co_return push(
      LexNode{LexKind::through_nested, open, base, 2, end.eof ? 1u : 0u});
}

std::uint16_t LexReifier::reify(ImageId id, bool tail) {
  return descend(reify_walk(id, tail));
}

Descent<std::uint16_t> LexReifier::reify_walk(ImageId id, bool tail) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::call:
      co_return push(
          LexNode{LexKind::call, kNoLexNode, static_cast<std::uint16_t>(n.m)});
    case ImageKind::ref:
      co_return reify_ref(id);
    case ImageKind::lit:
      co_return co_await reify_lit(id, tail);
    case ImageKind::notOf:
      co_return co_await reify_not_of(child_at(n, 0));
    case ImageKind::andOf: {
      const std::uint16_t node = co_await reify_walk(child_at(n, 0), true);
      co_return push(LexNode{LexKind::positive, node});
    }
    case ImageKind::eof:
      co_return reify_eof();
    case ImageKind::indent:
      co_return push(LexNode{LexKind::indent, 0, 0, 0, 0});
    case ImageKind::dedent:
      co_return push(LexNode{LexKind::dedent, 0, 0, 0, 0});
    case ImageKind::byte:
      co_return push(LexNode{LexKind::chars, intern(kAllBytes), 0, 0, 0});
    case ImageKind::cpset:
      co_return reify_class(id);
    case ImageKind::seq:
    case ImageKind::choice: {
      const bool choice = n.kind == ImageKind::choice;
      const auto children = _lowerer.children_of(n);
      std::vector<std::uint16_t> arms;
      for (std::size_t i = 0; i < children.size(); ++i)
        arms.push_back(co_await reify_walk(
            children[i], tail && (choice || i + 1 == children.size())));
      co_return group(choice ? LexKind::alt : LexKind::seq, arms);
    }
    case ImageKind::mark:
      co_return co_await reify_mark(id, tail);
    case ImageKind::same:
      co_return push(LexNode{LexKind::same, 0, 0,
                             _c.intern_tag(*static_cast<const Tag*>(n.tag)),
                             0});
    case ImageKind::through:
      co_return co_await reify_through(id, tail);
    case ImageKind::throughNested:
      co_return co_await reify_nested(id);
    case ImageKind::commit:
      refuse_lowering(
          "a commit is allowed in a rule, not in a terminal or a gap condition "
          "-- use the plain repetition forms there");
    case ImageKind::repeat: {
      if (n.m >= kEndless || (n.n != kUnbounded && n.n >= kEndless))
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      const std::uint16_t childNode =
          co_await reify_walk(child_at(n, 0), false);
      co_return push(LexNode{
          LexKind::rep, childNode, kNoLexNode, static_cast<std::uint32_t>(n.m),
          n.n == kUnbounded ? kEndless : static_cast<std::uint32_t>(n.n)});
    }
    case ImageKind::embed:
      refuse_lowering(
          "`section<G>(open, close)` is allowed in a rule, not in a terminal: a "
          "terminal is one token, and a section parses a grammar");
    case ImageKind::doc:
      refuse_lowering(
          "a `doc` goes on a literal a rule reads, not inside a terminal or a "
          "guard: those are part of one token -- document the token where a "
          "rule reads it");
    default:
      refuse_lowering(
          "this expression cannot be used inside a terminal: a terminal "
          "matches one token, so it cannot hold an unordered group, `skip`, "
          "`tight`, a gap condition, `implied` or a directive");
  }
}

}  // namespace cursiv::engine
