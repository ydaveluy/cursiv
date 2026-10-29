#include "cursiv/engines/shared/gen/LexReifier.hpp"

#include <algorithm>
#include <utility>

#include "cursiv/core/grammar/GrammarPrinter.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/grammar/word_final_in.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/SetIndex.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/Lowerer.hpp"

namespace cursiv {

namespace {

ByteSet byte_range(unsigned lo, unsigned hi) {
  ByteSet s;
  s.add(lo, hi);
  return s;
}

// A case-insensitive literal is the same token graph with two bytes per
// letter: folding is the identity on everything else.
ByteSet case_set(char ch, bool ci) {
  ByteSet bs;
  bs.add(static_cast<unsigned char>(ch));
  if (ci) {
    if (ch >= 'a' && ch <= 'z')
      bs.add(static_cast<unsigned char>(ch - 'a' + 'A'));
    else if (ch >= 'A' && ch <= 'Z')
      bs.add(static_cast<unsigned char>(ch - 'A' + 'a'));
  }
  return bs;
}

ByteSet class_bytes(std::string_view spec) {
  return bytes_of(detail::parse_charset(spec));
}

LexCodePoints cps_of(const ImageNode& n) {
  return LexCodePoints::of(n.m, (n.flags & kCpNot) != 0, n.text);
}

bool ascii_class(const ImageNode& n) {
  return n.m == 0 && detail::cp_ascii_of(n.text);
}

bool one_character(std::string_view s) {
  std::size_t len = 0;
  const char32_t cp = scalar_at(s.data(), s.size(), len);
  return s.size() == 1 || (cp != kNoScalar && len == s.size());
}

}  // namespace

LexReifier::LexReifier(Lowerer& lowerer, ByteSet& wordBytes,
                       detail::SetIndex& sets)
    : _lowerer(lowerer), _im(lowerer._im), _c(lowerer._c),
      _wordBytes(wordBytes), _x(sets) {}

std::uint16_t LexReifier::intern(const ByteSet& set, std::uint16_t decode) {
  return detail::intern_set(_c, _x, set, decode);
}

std::uint16_t LexReifier::group(LexKind kind,
                                std::span<const std::uint16_t> kids,
                                std::uint32_t min) {
  const std::uint16_t base = _c.nlexKid;
  for (const std::uint16_t k : kids) push_kid(k);
  return push(
      LexNode{kind, base, static_cast<std::uint16_t>(kids.size()), min, 0});
}

std::uint16_t LexReifier::refusal(std::uint16_t bound,
                                  std::span<const std::uint16_t> classes,
                                  std::uint32_t scalar) {
  const std::uint16_t base = _c.nlexKid;
  for (const std::uint16_t c : classes) push_kid(c);
  return push(LexNode{LexKind::nguard, bound, base,
                      static_cast<std::uint32_t>(classes.size()), scalar});
}

ImageId LexReifier::make(ImageKind kind, std::span<const ImageId> kids,
                         std::uint8_t flags, std::uint32_t a, std::uint64_t m,
                         std::uint64_t n, std::string_view text) {
  const ImageNode shape{
      .kind = kind, .flags = flags, .a = a, .m = m, .n = n, .text = text};
  return _im.derive(shape, kids);
}

ImageId LexReifier::make_class(const ImageClassDesc& cls) {
  return make(ImageKind::cpset, {},
              static_cast<std::uint8_t>(cls.complement ? kCpNot : 0), kNoImage,
              cls.props, 0, cls.scalars);
}

ImageId LexReifier::rebuilt(ImageId id, const std::vector<ImageId>& kids) {
  if (std::ranges::equal(_im.shape_kids(id), kids)) return id;
  return _im.derive(ImageNode{at(id)}, kids);
}

ImageId LexReifier::lit_with(const ImageNode& lit, ImageId guard) {
  ImageNode shape = lit;
  shape.flags = static_cast<std::uint8_t>(
      (lit.flags & kLitFolded) | (guard != kNoImage ? kLitGuardExpr : 0));
  const ImageId kids[] = {guard};
  return _im.derive(shape, std::span(kids, guard != kNoImage ? 1 : 0));
}

ImageId LexReifier::canon(ImageId id) {
  if (const auto it = _canon.find(id); it != _canon.end()) return it->second;
  const ImageNode n = at(id);
  std::vector<ImageId> ks;
  for (std::uint32_t i = 0; i < n.count; ++i) ks.push_back(canon(kid(n, i)));
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
  return at->second;
}

ImageId LexReifier::resolve(unsigned cfg, ImageId id,
                            std::span<const std::uint32_t> path) {
  Path p(path.begin(), path.end());
  return resolve_in(cfg, id, p);
}

ImageId LexReifier::resolve_rule(unsigned cfg, std::uint32_t rule, Path& path) {
  const auto call = [&] {
    const auto at = _lowerer._termIdx.find(rule);
    const std::uint32_t term =
        at == _lowerer._termIdx.end() ? kNoImage : at->second;
    if (term == kNoImage)
      refuse_lowering("cursiv: a lexical call names an unreached terminal");
    return make(ImageKind::call, {}, 0, kNoImage, 1u + term);
  };
  if (std::ranges::find(path, rule) != path.end()) return call();
  const ImageId body = terminal_body(rule, _lowerer.body_in(cfg, rule));
  path.push_back(rule);
  const ImageId resolved = resolve_in(cfg, body, path);
  path.pop_back();
  return has_captures(resolved) ? call() : resolved;
}

ImageId LexReifier::resolve_in(unsigned cfg, ImageId id, Path& path) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::ref:
      if (n.a != kNoImage && _lowerer.terminal_rule(n.a))
        return resolve_rule(cfg, n.a, path);
      return id;
    case ImageKind::lit: {
      if ((n.flags & kLitGuardExpr) == 0) return id;
      const ImageId g = kid(n, 0);
      const ImageId r = resolve_in(cfg, g, path);
      return r == g ? id : lit_with(n, r);
    }
    case ImageKind::notOf:
      if (at(kid(n, 0)).kind == ImageKind::byte)
        return make(ImageKind::eof);
      break;
    case ImageKind::seq:
      return resolve_seq(cfg, id, path);
    case ImageKind::sectionClose: {
      const ImageId end = _lowerer._cfgs[cfg].end;
      if (end == kNoImage)
        refuse_lowering("cursiv: a section's close is read outside any section");
      return resolve_in(cfg, end, path);
    }
    default:
      break;
  }
  const auto shape = _im.shape_kids(id);
  std::vector<ImageId> ks(shape.begin(), shape.end());
  for (ImageId& k : ks) k = resolve_in(cfg, k, path);
  return rebuilt(id, ks);
}

bool LexReifier::consumes(ImageId id) const {
  const ImageNode& n = at(id);
  switch (n.kind) {
    case ImageKind::lit:
    case ImageKind::cpset:
    case ImageKind::byte:
      return true;
    case ImageKind::seq:
      return std::ranges::any_of(_lowerer.kids_of(n),
                                 [&](ImageId k) { return consumes(k); });
    case ImageKind::choice:
      return std::ranges::all_of(_lowerer.kids_of(n),
                                 [&](ImageId k) { return consumes(k); });
    case ImageKind::repeat:
      return n.m > 0 && consumes(kid(n, 0));
    default:
      return false;
  }
}

bool LexReifier::eof_lit(ImageId id) const {
  const ImageNode& n = at(id);
  if (n.kind != ImageKind::lit || (n.flags & kLitGuardExpr) == 0) return false;
  const ImageNode& g = at(kid(n, 0));
  if (g.kind != ImageKind::notOf) return false;
  const ImageNode& k = at(kid(g, 0));
  if (k.kind == ImageKind::eof) return true;
  return k.kind == ImageKind::choice &&
         std::ranges::any_of(_lowerer.kids_of(k), [&](ImageId c) {
           return at(c).kind == ImageKind::eof;
         });
}

ImageId LexReifier::without_eof(ImageId id) {
  const ImageNode n = at(id);
  if ((n.flags & kLitGuardExpr) == 0) return id;
  const ImageNode g = at(kid(n, 0));
  if (g.kind != ImageKind::notOf) return id;
  const ImageNode k = at(kid(g, 0));
  if (k.kind == ImageKind::eof) return lit_with(n, kNoImage);
  if (k.kind != ImageKind::choice) return id;
  std::vector<ImageId> rest;
  for (const ImageId l : _lowerer.kids_of(k))
    if (at(l).kind != ImageKind::eof) rest.push_back(l);
  ImageId guard = kNoImage;
  if (rest.size() == 1)
    guard = make_not(rest.front());
  else if (rest.size() > 1)
    guard = make_not(make(ImageKind::choice, rest));
  if (guard != kNoImage && canon(guard) == canon(kid(n, 0))) return id;
  return lit_with(n, guard);
}

ImageId LexReifier::resolve_seq(unsigned cfg, ImageId id, Path& path) {
  const ImageNode n = at(id);
  std::vector<ImageId> es;
  for (const ImageId k : _lowerer.kids_of(n))
    es.push_back(resolve_in(cfg, k, path));
  const bool eof = std::ranges::any_of(
      es, [&](ImageId e) { return eof_lit(e); });
  if (eof) {
    const auto last = std::find_if(es.rbegin(), es.rend(),
                                   [&](ImageId e) { return consumes(e); });
    if (last != es.rend())
      for (auto i = es.begin(); i != last.base() - 1; ++i)
        if (at(*i).kind == ImageKind::lit) *i = without_eof(*i);
  }
  return rebuilt(id, es);
}

bool LexReifier::has_captures(ImageId id) const {
  const ImageNode& n = at(id);
  switch (n.kind) {
    case ImageKind::mark:
    case ImageKind::same:
      return true;
    case ImageKind::lit:
      return (n.flags & kLitGuardExpr) != 0 && has_captures(kid(n, 0));
    default:
      for (const ImageId k : _im.shape_kids(id))
        if (has_captures(k)) return true;
      return false;
  }
}

LexReifier::EndCloser LexReifier::end_closer(ImageId close) {
  const ImageNode n = at(close);
  if (n.kind != ImageKind::choice) return {close, false};
  const std::vector<ImageId> ks = _lowerer.kids_of(n);
  if (ks.size() < 2 || at(ks.back()).kind != ImageKind::eof)
    return {close, false};
  if (ks.size() == 2) return {ks[0], true};
  std::vector<ImageId> joined;
  for (std::size_t i = 0; i + 1 < ks.size(); ++i) {
    if (at(ks[i]).kind == ImageKind::choice)
      for (const ImageId k : _lowerer.kids_of(at(ks[i]))) joined.push_back(k);
    else
      joined.push_back(ks[i]);
  }
  return {make(ImageKind::choice, joined), true};
}

// The native search finds a delimiter by its first byte anywhere in the text,
// where `any` tests delimiters only at the start of a character. A literal
// that starts with a UTF-8 continuation byte may stand inside a character, so
// it keeps the general reading.
bool LexReifier::nested_literals(ImageId id,
                                 std::vector<std::string_view>& out) const {
  const ImageNode& n = at(id);
  if (n.kind == ImageKind::lit) {
    if (n.flags != 0 ||
        (static_cast<unsigned char>(n.text[0]) & 0xC0u) == 0x80u)
      return false;
    out.push_back(n.text);
    return true;
  }
  if (n.kind != ImageKind::choice || n.count < 2) return false;
  for (std::uint32_t i = 0; i < n.count; ++i)
    if (!nested_literals(kid(n, i), out)) return false;
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
    a = kid(n, 0);
    return true;
  };
  const auto any_char = [&](ImageId r) {
    const ImageNode& n = at(r);
    return n.kind == ImageKind::cpset && n.m == 0 &&
           (n.flags & kCpNot) != 0 && n.text.empty();
  };
  const auto both = [&](ImageId seq) {
    const ImageNode& n = at(seq);
    ImageId a = kNoImage;
    ImageId b = kNoImage;
    if (n.kind != ImageKind::seq || n.count != 3 || !refuses(kid(n, 0), a) ||
        !refuses(kid(n, 1), b) || !any_char(kid(n, 2)))
      return false;
    return (same(a, open) && closes(b)) || (same(b, open) && closes(a));
  };
  const ImageNode& n = at(item);
  if (n.kind == ImageKind::choice && n.count == 2) {
    const ImageId x = kid(n, 0);
    const ImageId y = kid(n, 1);
    if (calls(x)) return both(y);
    if (calls(y)) return both(x);
    return false;
  }
  if (n.kind == ImageKind::seq && n.count == 2) {
    ImageId a = kNoImage;
    if (!refuses(kid(n, 0), a)) return false;
    const ImageNode& c = at(kid(n, 1));
    if (c.kind != ImageKind::choice || c.count != 2 || !calls(kid(c, 0)))
      return false;
    const ImageNode& s = at(kid(c, 1));
    ImageId o = kNoImage;
    if (s.kind != ImageKind::seq || s.count != 2 || !refuses(kid(s, 0), o) ||
        !same(o, open) || !any_char(kid(s, 1)))
      return false;
    return closes(a);
  }
  return false;
}

ImageId LexReifier::terminal_body(std::uint32_t rule, ImageId body) {
  const ImageNode& b = at(body);
  if (b.kind != ImageKind::seq || b.count != 3) return body;
  const ImageNode& rep = at(kid(b, 1));
  if (rep.kind != ImageKind::repeat || rep.m != 0 || rep.n != kUnbounded)
    return body;
  const ImageId open = kid(b, 0);
  const ImageId close = kid(b, 2);
  if (!nested_item(rule, open, close, kid(rep, 0)) ||
      !nested_unambiguous(open, close))
    return body;
  const ImageId ends[] = {open, close};
  return make(ImageKind::throughNested, ends);
}

// The key of a literal's token. A keyword's is the literal refusing its word
// boundary, complement included: it is one token with the literal that spells
// that guard, and another with the literal that refuses the class it leaves
// out.
ImageId LexReifier::token_of(unsigned cfg, ImageId id,
                             const ImageClassDesc& bound) {
  const ImageNode n = at(id);
  if (n.flags & kLitGuardWord) {
    const bool ends = detail::word_final_in(
        n.text, bound.props, bound.complement, bound.scalars,
        (n.flags & kLitFolded) != 0);
    return lit_with(n, ends ? make_not(make_class(bound)) : kNoImage);
  }
  if (n.flags & kLitGuardExpr) return lit_with(n, resolve(cfg, kid(n, 0)));
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

bool LexReifier::word_final(std::string_view s, bool ci) const {
  if (s.empty()) return false;
  const auto last = static_cast<unsigned char>(s.back());
  if (_c.wordRanges.size() != 0 && last >= 0x80) {
    std::size_t at = s.size();
    while (at > 0 && (static_cast<unsigned char>(s[at - 1]) & 0xC0u) == 0x80u)
      --at;
    if (at > 0) --at;
    std::size_t len = 0;
    const char32_t cp = scalar_at(s.data() + at, s.size() - at, len);
    return cp != kNoScalar && at + len == s.size() &&
           unicode::within(_c.wordRanges.data(), _c.wordRanges.size(), cp);
  }
  return case_set(static_cast<char>(last), ci).intersects(_wordBytes);
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
  const ImageId k = kid(n, 0);
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
    const std::vector<ImageId> ks = _lowerer.kids_of(kn);
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
  if (lit.flags & kLitGuardExpr) return classify(kid(lit, 0));
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
  return g.word && !word_final(lit.text, (lit.flags & kLitFolded) != 0)
             ? kNoSet
             : guard_set(g, tail);
}

std::uint16_t LexReifier::class_guard(std::uint16_t guard) {
  for (std::uint16_t i = 0; i < _c.nlex; ++i)
    if (_c.lex[i].kind == LexKind::nguard && _c.lex[i].a == kNoSet &&
        _c.lex[i].min == 1 && _c.lexKid[_c.lex[i].b] == guard)
      return i;
  return refusal(kNoSet, std::span(&guard, 1));
}

std::uint16_t LexReifier::refused(ImageId word, std::uint16_t node) {
  _c.lexSpelled[node] = GrammarPrinter(_im, ImageView::written).spell(word);
  return node;
}

void LexReifier::class_refusals(ImageId id,
                                std::vector<std::uint16_t>& out) {
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

void LexReifier::refusals(const Guard& g, std::vector<std::uint16_t>& out) {
  for (const ImageId w : g.refused) {
    if (at(w).kind == ImageKind::cpset)
      class_refusals(w, out);
    else
      out.push_back(reify_not_lit(w));
  }
}

std::uint16_t LexReifier::reify_lit(ImageId id, bool tail) {
  const ImageNode n = at(id);
  const Guard g = guard_of(n);
  const std::uint16_t guard = lit_guard(n, g, tail);
  const bool guarded = guard != kNoSet;
  const std::uint16_t barred = guarded ? class_guard(guard) : 0;
  std::vector<std::uint16_t> words;
  if (g.words && !guarded) refusals(g, words);
  if (g.expression)
    words.push_back(push(LexNode{LexKind::positive, reify(g.node)}));
  std::vector<std::uint16_t> kids;
  for (const char ch : n.text)
    kids.push_back(push(
        LexNode{LexKind::chars,
                intern(case_set(ch, (n.flags & kLitFolded) != 0)), 0, 0, 0}));
  if (guarded) kids.push_back(barred);
  kids.insert(kids.end(), words.begin(), words.end());
  return group(LexKind::seq, kids, 1);
}

std::uint16_t LexReifier::reify_not_lit(ImageId id) {
  const ImageNode n = at(id);
  const Guard g = guard_of(n);
  if ((g.expression || g.words) && lit_guard(n, g, true) == kNoSet)
    return push(LexNode{LexKind::negative, reify_lit(id, true)});
  std::vector<std::uint16_t> classes;
  for (const char ch : n.text)
    classes.push_back(intern(case_set(ch, (n.flags & kLitFolded) != 0)));
  return refused(id, refusal(lit_guard(n, g, true), classes));
}

std::uint16_t LexReifier::reify_not_class(ImageId id) {
  const ImageNode n = at(id);
  if (!refusable(id)) {
    const ImageId plain = make(ImageKind::cpset, {},
                               static_cast<std::uint8_t>(n.flags & ~kCpNot),
                               kNoImage, n.m, 0, n.text);
    const std::uint16_t arms[] = {reify_class(plain), reify_eof()};
    return push(LexNode{LexKind::positive, group(LexKind::alt, arms)});
  }
  std::vector<std::uint16_t> nodes;
  class_refusals(id, nodes);
  return nodes.size() == 1 ? nodes.front() : group(LexKind::seq, nodes);
}

std::uint16_t LexReifier::reify_not_of(ImageId id) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::lit:
      return reify_not_lit(id);
    case ImageKind::cpset:
      return reify_not_class(id);
    case ImageKind::choice: {
      std::vector<std::uint16_t> arms;
      for (const ImageId k : _lowerer.kids_of(n))
        arms.push_back(reify_not_of(k));
      return group(LexKind::seq, arms);
    }
    default:
      return push(LexNode{LexKind::negative, reify(id)});
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
    std::uint16_t kids[4];
    for (unsigned k = 0; k < q.len; ++k)
      kids[k] = push(LexNode{LexKind::chars,
                             intern(byte_range(q.lo[k], q.hi[k])), 0, 0, 0});
    alts.push_back(group(LexKind::seq, std::span(kids, q.len)));
  }
  if (const ByteSet bytes = cps.bytes(); bytes.any())
    alts.push_back(push(LexNode{LexKind::chars, intern(bytes), 0, 0, 0}));
  if (cps.strays()) {
    for (const LexCodePoints::StrayLead& lead : LexCodePoints::stray_leads()) {
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
  if (n.kind != ImageKind::repeat) return false;
  const ImageNode& k = at(kid(n, 0));
  if (k.kind == ImageKind::cpset) return true;
  return k.kind == ImageKind::lit && k.flags == 0 && one_character(k.text);
}

std::uint16_t LexReifier::reify_mark(ImageId id, bool tail) {
  const ImageNode n = at(id);
  if (!mark_run(kid(n, 0)))
    refuse_lowering(
        "`mark<>` takes one character class or one-character literal, "
        "alone or repeated: `many(lit<\"=\">)`, `some(w)`, "
        "`repeat<0, 16>(cr<\"^() \">)`");
  const std::uint16_t kidNode = reify(kid(n, 0), tail);
  return push(LexNode{
      LexKind::mark, kidNode, 0,
      _c.intern_tag(*static_cast<const detail::Tag*>(n.tag)), 0});
}

std::uint16_t LexReifier::reify_ref(ImageId id) {
  const ImageNode n = at(id);
  const std::uint16_t node = push(LexNode{LexKind::seq, _c.nlexKid, 0, 0, 0});
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
         at(kid(n, 0)).kind == ImageKind::cpset;
}

LexReifier::Echo LexReifier::echo_closer(ImageId close) const {
  const ImageNode& n = at(close);
  if (n.kind != ImageKind::seq) return {};
  const std::vector<ImageId> ks = _lowerer.kids_of(n);
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

std::uint16_t LexReifier::reify_through_lit(ImageId open,
                                            std::string_view close,
                                            bool eof) {
  const std::size_t n = close.size();
  const std::uint16_t openNode = reify(open, false);
  const std::uint16_t end = eof ? class_guard(intern(kAllBytes)) : kNoLexNode;

  std::vector<std::size_t> fail(n + 1, 0);
  for (std::size_t i = 1, k = 0; i < n; ++i) {
    while (k > 0 && close[i] != close[k]) k = fail[k - 1];
    if (close[i] == close[k]) ++k;
    fail[i] = k;
  }

  const std::uint16_t base = _c.nlexKid;
  std::vector<ByteSet> to(n + 1);
  for (std::size_t i = 0; i < n; ++i) {
    std::ranges::fill(to, ByteSet{});
    for (std::size_t b = 0; b < 256; ++b) {
      std::size_t k = i;
      while (k > 0 && static_cast<char>(b) != close[k]) k = fail[k - 1];
      to[(static_cast<char>(b) == close[k]) ? k + 1 : 0].add(
          static_cast<unsigned char>(b));
    }
    for (const ByteSet& bs : to)
      push_kid(bs.any() ? intern(bs) : static_cast<std::uint16_t>(0xffff));
  }
  if (end != kNoLexNode) push_kid(end);
  return push(LexNode{LexKind::through_lit, openNode, base,
                      static_cast<std::uint32_t>(n), eof ? 1u : 0u});
}

std::uint16_t LexReifier::reify_through(ImageId id, bool tail) {
  const ImageNode n = at(id);
  const ImageId open = kid(n, 0);
  const ImageId close = kid(n, 1);
  const Echo echo = echo_closer(close);
  if (echo.echoes) {
    const std::uint16_t openNode = reify(open, false);
    const std::uint16_t before = echo_side(echo.before);
    const std::uint16_t after = echo_side(echo.after);
    const std::uint16_t pad = echo_side(echo.pad);
    const std::uint16_t base = _c.nlexKid;
    push_kid(before);
    push_kid(after);
    if (pad != kNoLexNode) push_kid(pad);
    return push(LexNode{
        LexKind::through_same, openNode, base, pad != kNoLexNode ? 3u : 2u,
        _c.intern_tag(*static_cast<const detail::Tag*>(echo.tag))});
  }
  const EndCloser end = end_closer(close);
  const ImageNode& closer = at(end.type);
  if (closer.kind == ImageKind::lit && closer.flags == 0 &&
      !closer.text.empty() &&
      (static_cast<unsigned char>(closer.text[0]) & 0xC0) != 0x80)
    return reify_through_lit(open, closer.text, end.eof);
  const ImageId notClose = make_not(close);
  const ImageId anyChar =
      make(ImageKind::cpset, {}, static_cast<std::uint8_t>(kCpNot));
  const ImageId item[] = {notClose, anyChar};
  const ImageId itemSeq = make(ImageKind::seq, item);
  const ImageId rep = make(ImageKind::repeat, std::span(&itemSeq, 1), 0,
                           kNoImage, 0, kUnbounded);
  const ImageId parts[] = {open, rep, close};
  return reify(make(ImageKind::seq, parts), tail);
}

std::uint16_t LexReifier::reify_nested(ImageId id) {
  const ImageNode n = at(id);
  const EndCloser end = end_closer(kid(n, 1));
  const std::uint16_t open = reify(kid(n, 0), false);
  const std::uint16_t close = reify(end.type, false);
  const std::uint16_t base = _c.nlexKid;
  push_kid(open);
  push_kid(close);
  return push(LexNode{LexKind::through_nested, open, base, 2,
                      end.eof ? 1u : 0u});
}

std::uint16_t LexReifier::reify(ImageId id, bool tail) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::call:
      return push(LexNode{LexKind::call, kNoLexNode,
                          static_cast<std::uint16_t>(n.m)});
    case ImageKind::ref:
      return reify_ref(id);
    case ImageKind::lit:
      return reify_lit(id, tail);
    case ImageKind::notOf:
      return reify_not_of(kid(n, 0));
    case ImageKind::andOf:
      return push(LexNode{LexKind::positive, reify(kid(n, 0))});
    case ImageKind::eof:
      return reify_eof();
    case ImageKind::indent:
      return push(LexNode{LexKind::indent, 0, 0, 0, 0});
    case ImageKind::dedent:
      return push(LexNode{LexKind::dedent, 0, 0, 0, 0});
    case ImageKind::byte:
      return push(LexNode{LexKind::chars, intern(kAllBytes), 0, 0, 0});
    case ImageKind::cpset:
      return reify_class(id);
    case ImageKind::seq:
    case ImageKind::choice: {
      const bool choice = n.kind == ImageKind::choice;
      const auto kids = _lowerer.kids_of(n);
      std::vector<std::uint16_t> arms;
      for (std::size_t i = 0; i < kids.size(); ++i)
        arms.push_back(reify(kids[i], tail && (choice || i + 1 == kids.size())));
      return group(choice ? LexKind::alt : LexKind::seq, arms);
    }
    case ImageKind::mark:
      return reify_mark(id, tail);
    case ImageKind::same:
      return push(LexNode{
          LexKind::same, 0, 0,
          _c.intern_tag(*static_cast<const detail::Tag*>(n.tag)),
          0});
    case ImageKind::through:
      return reify_through(id, tail);
    case ImageKind::throughNested:
      return reify_nested(id);
    case ImageKind::commit:
      refuse_lowering(
          "a commit is allowed in a rule, not in a terminal or a gap condition "
          "-- use the plain repetition forms there");
    case ImageKind::repeat: {
      if (n.m >= 0xffffffffu || (n.n != kUnbounded && n.n >= 0xffffffffu))
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      const std::uint16_t kidNode = reify(kid(n, 0), false);
      return push(LexNode{LexKind::rep, kidNode, kNoLexNode,
                          static_cast<std::uint32_t>(n.m),
                          n.n == kUnbounded
                              ? 0xffffffffu
                              : static_cast<std::uint32_t>(n.n)});
    }
    case ImageKind::embed:
      refuse_lowering(
          "`section<G>(open, close)` is allowed in a rule, not in a terminal: a "
          "terminal is one token, and a section parses a grammar");
    case ImageKind::doc:
      refuse_lowering(
          "a `doc` inside a guard has nothing to attach to: the guard is "
          "part of the token before it -- document the literal the guard "
          "belongs to");
    default:
      refuse_lowering(
          "this expression cannot be used inside a terminal: a terminal "
          "matches one token, so it cannot hold an unordered group, `skip`, "
          "`tight`, a gap condition, `implied` or a directive");
  }
}

}  // namespace cursiv
