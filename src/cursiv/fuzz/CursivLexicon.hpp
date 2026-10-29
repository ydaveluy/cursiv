// The lexicon of one compiled grammar, spelled from the expression its
// author wrote. A character range keeps its byte set in the type alone --
// the reified model does not carry it -- so the walk is templated and its
// answers erased.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/RuleTraits.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/grammar/word_final_in.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/fuzz/DecisionSource.hpp"
#include "cursiv/fuzz/DeclaredModel.hpp"
#include "cursiv/fuzz/FixedSource.hpp"
#include "cursiv/fuzz/GrammarLexicon.hpp"
#include "cursiv/fuzz/GrammarModel.hpp"

namespace cursiv::fuzzer {

// Rule calls a terminal may nest before the walk gives up, and the extra
// rounds a repetition takes.
inline constexpr unsigned kSpellDepth = 6;
inline constexpr std::size_t kSpellRepCap = 6;
// "This expression spells nothing", as a length no sum reaches.
inline constexpr std::size_t kNoSpell = ~std::size_t{0};

// The ribbon plus what the assertion in front forbids. `!cr<R"("\\n)"> + any`
// is the vocabulary's way of writing "any byte but these"; a speller that
// ignored the refusal could close a string literal in its own middle.
struct SpellCtx {
  DecisionSource& src;
  CharSet veto{};
  bool vetoed = false;

  void forbid(const CharSet& s) {
    veto.merge(s);
    vetoed = true;
  }
  void clear() {
    if (!vetoed) return;
    veto = CharSet{};
    vetoed = false;
  }
};

// Which byte of a set to spell; the two boundaries are named styles since
// a set is a range and a lexer's off-by-one lives at its ends.
[[nodiscard]] inline bool spell_byte(const CharSet& s, SpellStyle st,
                                     SpellCtx& cx, std::string& out) {
  int lo = -1, hi = -1, nice = -1;
  unsigned count = 0;
  const bool vetoed = cx.vetoed;
  const CharSet veto = cx.veto;
  cx.clear();
  const auto in = [&](unsigned b) {
    return s.test(static_cast<unsigned char>(b)) &&
           !(vetoed && veto.test(static_cast<unsigned char>(b)));
  };
  for (unsigned b = 0; b < 256; ++b) {
    if (!in(b)) continue;
    if (lo < 0) lo = static_cast<int>(b);
    hi = static_cast<int>(b);
    ++count;
    if (nice < 0 && b >= 0x21 && b <= 0x7e) nice = static_cast<int>(b);
  }
  if (lo < 0) return false;
  if (st == SpellStyle::low) {
    out.push_back(static_cast<char>(lo));
    return true;
  }
  if (st == SpellStyle::high) {
    out.push_back(static_cast<char>(hi));
    return true;
  }
  if (st == SpellStyle::random) {
    unsigned k = cx.src.choose(DecisionKind::byte, count);
    for (unsigned b = 0; b < 256; ++b)
      if (in(b) && k-- == 0) {
        out.push_back(static_cast<char>(b));
        return true;
      }
  }
  out.push_back(static_cast<char>(nice >= 0 ? nice : lo));
  return true;
}

[[nodiscard]] constexpr std::size_t spell_sum(
    std::initializer_list<std::size_t> parts) {
  std::size_t n = 0;
  for (const std::size_t p : parts) {
    if (p == kNoSpell) return kNoSpell;
    n += p;
  }
  return n;
}

// The ASCII characters of a class, when the expression is one.
template <std::uint64_t M, bool N, str S>
inline constexpr CharSet kClassAscii = [] {
  CharSet s;
  for (unsigned b = 0; b < 0x80; ++b)
    if (cursiv::detail::word_class<CpSet<M, N, S>>::has(b))
      s.set(static_cast<unsigned char>(b));
  return s;
}();
template <class E>
inline constexpr const CharSet* kAsciiOf = nullptr;
template <std::uint64_t M, bool N, str S>
inline constexpr const CharSet* kAsciiOf<CpSet<M, N, S>> =
    &kClassAscii<M, N, S>;

template <class C, class E, unsigned D>
struct SpellOf;

// The recursion's one door: an expression reaches the table as the author's
// `static constexpr auto`, so the reference and the const come off here.
template <class C, class E, unsigned D>
struct Spell : SpellOf<C, std::remove_cvref_t<E>, D> {};

// Where an expression can begin and end, in bytes, with nullability so a
// sequence knows how far the two sets reach.
struct Reach {
  CharSet first{};
  CharSet last{};
  bool nullable = false;
};

constexpr Reach reach_seq(std::initializer_list<Reach> parts) {
  Reach out;
  const Reach* const p = parts.begin();
  const std::size_t n = parts.size();
  bool run = true;
  for (std::size_t i = 0; i < n && run; ++i) {
    out.first.merge(p[i].first);
    run = p[i].nullable;
  }
  run = true;
  for (std::size_t i = n; i-- > 0 && run;) {
    out.last.merge(p[i].last);
    run = p[i].nullable;
  }
  out.nullable = true;
  for (std::size_t i = 0; i < n; ++i)
    out.nullable = out.nullable && p[i].nullable;
  return out;
}

constexpr Reach reach_alt(std::initializer_list<Reach> parts) {
  Reach out;
  for (const Reach& r : parts) {
    out.first.merge(r.first);
    out.last.merge(r.last);
    out.nullable = out.nullable || r.nullable;
  }
  return out;
}

constexpr Reach reach_one(const CharSet& s) { return Reach{s, s, false}; }

// Every byte, because that is what `any` accepts -- spelling it printable is
// the walk's own restraint, not the lexer's.
inline constexpr CharSet kEveryByte = [] {
  CharSet s;
  for (unsigned b = 0; b < 256; ++b) s.set(static_cast<unsigned char>(b));
  return s;
}();

// The vocabulary's own registry, walked for bytes. An expression with no
// spelling answers `false` rather than guessing one, so the terminal
// holding it is reported unspellable.
template <class C, class E, unsigned D>
struct SpellOf {
  static constexpr std::size_t min = kNoSpell;
  static constexpr Reach reach() { return {}; }
  static bool run(std::string&, SpellStyle, SpellCtx&) { return false; }
};

// A rule call spends one level of the budget; at the bottom it spells nothing.
template <class C, class E, unsigned D>
struct SpellDeeper {
  static constexpr std::size_t min = Spell<C, E, D - 1>::min;
  static constexpr Reach reach() { return Spell<C, E, D - 1>::reach(); }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return Spell<C, E, D - 1>::run(out, st, cx);
  }
};
template <class C, class E>
struct SpellDeeper<C, E, 0> {
  static constexpr std::size_t min = kNoSpell;
  static constexpr Reach reach() { return {}; }
  static bool run(std::string&, SpellStyle, SpellCtx&) { return false; }
};

template <class C, str S, bool CI, class Gd, unsigned D>
struct SpellOf<C, Lit<S, CI, Gd>, D> {
  static constexpr std::size_t min = S.size();
  static constexpr Reach reach() {
    CharSet f, l;
    f.set(static_cast<unsigned char>(S[0]));
    l.set(static_cast<unsigned char>(S[S.size() - 1]));
    return Reach{f, l, false};
  }
  static bool run(std::string& out, SpellStyle, SpellCtx& cx) {
    cx.clear();
    out += S.view();
    return true;
  }
};

// Prose spells nothing: the word under it is what the walk writes.
template <class C, class E, str T, unsigned D>
struct SpellOf<C, Doc<E, T>, D> : SpellOf<C, E, D> {};

// A class is spelled in ASCII where it holds any, else by its first scalar.
template <class C, std::uint64_t M, bool N, str S, unsigned D>
struct SpellOf<C, CpSet<M, N, S>, D> {
  static constexpr bool ascii = kClassAscii<M, N, S>.count() != 0;
  static constexpr std::size_t min = ascii ? 1 : 2;
  static constexpr Reach reach() {
    CharSet s = kClassAscii<M, N, S>;
    if (!ascii)
      for (unsigned b = 0xC2; b <= 0xF4; ++b)
        s.set(static_cast<unsigned char>(b));
    return reach_one(s);
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    if (ascii) return spell_byte(kClassAscii<M, N, S>, st, cx, out);
    cx.clear();
    for (char32_t cp = 0x80; cp <= 0x10FFFF; ++cp)
      if (cursiv::detail::word_class<CpSet<M, N, S>>::has(cp)) {
        append_scalar(out, cp);
        return true;
      }
    return false;
  }
};

// `any` is every byte; spelling it outside the printable range would put
// control bytes in a document nothing asked to test.
inline constexpr CharSet kPrintable = [] {
  CharSet s;
  for (unsigned b = 0x21; b <= 0x7e; ++b) s.set(static_cast<unsigned char>(b));
  return s;
}();

template <class C, unsigned D>
struct SpellOf<C, Byte, D> {
  static constexpr std::size_t min = 1;
  static constexpr Reach reach() { return reach_one(kEveryByte); }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return spell_byte(kPrintable, st, cx, out);
  }
};

template <class C, unsigned D>
struct SpellOf<C, AnyChar, D> : SpellOf<C, Byte, D> {};

template <class C, unsigned D>
struct SpellOf<C, Epsilon, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, SpellCtx&) { return true; }
};

template <class C, class... Es, unsigned D>
struct SpellOf<C, Seq<Es...>, D> {
  static constexpr std::size_t min = spell_sum({Spell<C, Es, D>::min...});
  static constexpr Reach reach() {
    return reach_seq({Spell<C, Es, D>::reach()...});
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return (Spell<C, Es, D>::run(out, st, cx) && ...);
  }
};

template <class C, class... Es, unsigned D>
struct SpellOf<C, Unordered<Es...>, D> {
  static constexpr std::size_t min = spell_sum({Spell<C, Es, D>::min...});
  static constexpr Reach reach() {
    return reach_alt({Spell<C, Es, D>::reach()...});
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return (Spell<C, Es, D>::run(out, st, cx) && ...);
  }
};

template <class C, class... Es, unsigned D>
struct SpellOf<C, Choice<Es...>, D> {
  using Fn = bool (*)(std::string&, SpellStyle, SpellCtx&);
  static constexpr std::size_t kMins[] = {Spell<C, Es, D>::min...};
  static constexpr Fn kAlts[] = {&Spell<C, Es, D>::run...};
  static constexpr std::size_t min = [] {
    std::size_t m = kNoSpell;
    for (const std::size_t v : kMins) m = std::min(v, m);
    return m;
  }();
  static constexpr Reach reach() {
    return reach_alt({Spell<C, Es, D>::reach()...});
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    constexpr unsigned n = sizeof...(Es);
    unsigned k = 0;
    if (st == SpellStyle::shortest) {
      for (unsigned i = 1; i < n; ++i)
        if (kMins[i] < kMins[k]) k = i;
    } else {
      k = cx.src.choose(DecisionKind::entry, n);
    }
    return kAlts[k](out, st, cx);
  }
};

template <class C, std::size_t Mn, std::size_t Mx, class E, unsigned D>
struct SpellOf<C, Repeat<Mn, Mx, E>, D> {
  static constexpr std::size_t min =
      Mn == 0 ? std::size_t{0}
              : (Spell<C, E, D>::min == kNoSpell ? kNoSpell
                                                 : Spell<C, E, D>::min * Mn);
  static constexpr Reach reach() {
    Reach r = Spell<C, E, D>::reach();
    r.nullable = r.nullable || Mn == 0;
    return r;
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    std::size_t n = Mn;
    if (st != SpellStyle::shortest && Mx > Mn) {
      const std::size_t span = std::min(Mx - Mn, kSpellRepCap) + 1;
      n = Mn + cx.src.choose(DecisionKind::byte, static_cast<unsigned>(span));
    }
    for (std::size_t i = 0; i < n; ++i)
      if (!Spell<C, E, D>::run(out, st, cx)) return false;
    return true;
  }
};

// What a refusal names, when it names a byte set: everything else the
// walk cannot honour, so a spelling the lexer then reads short is a
// refused document, not a silent one.
template <class E>
[[nodiscard]] constexpr const CharSet* vetoed_by() {
  return kAsciiOf<E>;
}

// An assertion spells nothing; the set it refuses travels to the byte in front
// of which it stands.
template <class C, class E, unsigned D>
struct SpellOf<C, Not<E>, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, [[maybe_unused]] SpellCtx& cx) {
    constexpr const CharSet* kVeto = vetoed_by<std::remove_cvref_t<E>>();
    if constexpr (kVeto != nullptr) cx.forbid(*kVeto);
    return true;
  }
};
template <class C, unsigned D>
struct SpellOf<C, Eof, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, SpellCtx&) { return true; }
};
template <class C, class E, unsigned D>
struct SpellOf<C, And<E>, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, SpellCtx&) { return true; }
};

// A run the end of the text also closes is spelled closed: the end of the
// text stands once, where the document ends.
template <class Cl>
struct closed_by {
  using type = Cl;
};
template <class L>
struct closed_by<Choice<L, Eof>> {
  using type = L;
};
template <class L0, class L1, class L2, class... Ls>
struct closed_by<Choice<L0, L1, L2, Ls...>> {
  using rest = Choice<L1, L2, Ls...>;
  using type = cursiv::detail::cond_t<
      std::is_same_v<typename closed_by<rest>::type, rest>,
      Choice<L0, L1, L2, Ls...>,
      typename cursiv::detail::ChoiceCat<
          L0, typename closed_by<rest>::type>::type>;
};

template <class C, class O, class Cl0, unsigned D>
struct SpellOf<C, Through<O, Cl0>, D> {
  using Cl = typename closed_by<Cl0>::type;
  static constexpr std::size_t min =
      spell_sum({Spell<C, O, D>::min, Spell<C, Cl, D>::min});
  static constexpr Reach reach() {
    return Reach{Spell<C, O, D>::reach().first, Spell<C, Cl, D>::reach().last,
                 false};
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return Spell<C, O, D>::run(out, st, cx) &&
           Spell<C, Cl, D>::run(out, st, cx);
  }
};

template <class C, class R, unsigned D>
struct SpellOf<C, Ref<R>, D>
    : SpellDeeper<C, typename cursiv::Normalize<C>::template body<R>, D> {};

template <class C, class I, class H, class E, unsigned D>
struct SpellOf<C, Skip<I, H, E>, D> : Spell<C, E, D> {};
template <class C, auto Sp, class E, unsigned D>
struct SpellOf<C, Assign<Sp, E>, D> : Spell<C, E, D> {};
template <class C, auto Sp, class E, unsigned D>
struct SpellOf<C, Append<Sp, E>, D> : Spell<C, E, D> {};
template <class C, auto P, class E, unsigned D>
struct SpellOf<C, Flag<P, E>, D> : Spell<C, E, D> {};
template <class C, class T, class E, unsigned D>
struct SpellOf<C, Create<T, E>, D> : Spell<C, E, D> {};
template <class C, class T, auto P, class E, unsigned D>
struct SpellOf<C, Nest<T, P, E>, D> : Spell<C, E, D> {};

template <class C, class R>
bool spell_terminal(std::string& out, SpellStyle st, DecisionSource& src) {
  SpellCtx cx{src};
  return Spell<C, typename cursiv::Normalize<C>::template body<R>,
               kSpellDepth>::run(out, st, cx);
}

template <class C, class R>
constexpr Reach reach_terminal() {
  return Spell<C, typename cursiv::Normalize<C>::template body<R>,
               kSpellDepth>::reach();
}

using SpellFn = bool (*)(std::string&, SpellStyle, DecisionSource&);
using ReachFn = Reach (*)();

template <class C, class E>
consteval SpellFn spell_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (is_terminal_rule<T>) return &spell_terminal<C, T>;
  }
  return nullptr;
}

// A function and not the set: storing a `Reach` per element would bloat
// the table, and only the separator -- known at construction -- turns
// one into the single bit the walk reads.
template <class C, class E>
consteval ReachFn reach_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (is_terminal_rule<T>) return &reach_terminal<C, T>;
  }
  return nullptr;
}

// A region that ignores nothing is an adjacency island: its tokens run
// into one another with no trivia admitted between them. Substitution has
// already resolved the region's configuration to rule lists.
template <class E>
inline constexpr bool glues_here = false;
template <class H, class E>
inline constexpr bool glues_here<Skip<TypeList<>, H, E>> = true;

template <class C, class L>
struct lex_rows;
template <class C, class... Es>
struct lex_rows<C, TypeList<Es...>> {
  static constexpr std::array<SpellFn, sizeof...(Es)> kSpell = {
      spell_entry<C, Es>()...};
  static constexpr std::array<bool, sizeof...(Es)> kGlue = {
      glues_here<std::remove_cvref_t<Es>>...};
  static constexpr std::array<ReachFn, sizeof...(Es)> kReach = {
      reach_entry<C, Es>()...};
};

template <class C, class L>
struct lex_separator {
  static std::string of() { return {}; }
};
template <class C, class R0, class... Rs>
struct lex_separator<C, TypeList<R0, Rs...>> {
  static std::string of() {
    FixedSource none;
    SpellCtx cx{none};
    std::string out;
    if (!Spell<C, typename cursiv::Normalize<C>::template body<R0>,
               kSpellDepth>::run(out, SpellStyle::shortest, cx))
      return {};
    return out;
  }
};

template <class C>
struct CursivLexicon final : GrammarLexicon {
  // The literal column comes off the erased model: a spelling the grammar owns
  // is the same fact whichever door answers it.
  explicit CursivLexicon(const GrammarModel& g)
      : _sep(lex_separator<C, typename cursiv::Normalize<C>::ignored>::of()) {
    using Rows = lex_rows<C, typename model::positions_for<C>::type>;
    _devours.assign(Rows::kReach.size(), false);
    for (std::size_t e = 0; e < Rows::kReach.size(); ++e) {
      if (Rows::kReach[e] == nullptr) continue;
      const Reach r = Rows::kReach[e]();
      for (const char c : _sep) {
        const auto b = static_cast<unsigned char>(c);
        _devours[e] = _devours[e] || r.first.test(b) || r.last.test(b);
      }
    }
    for (ElementId e = 0; e < g.positionCount(); ++e) {
      const std::string_view t = g.textOf(e);
      if (!t.empty()) _reserved.push_back(folded(t));
    }
    std::sort(_reserved.begin(), _reserved.end());
    _reserved.erase(std::unique(_reserved.begin(), _reserved.end()),
                    _reserved.end());
  }

  [[nodiscard]] std::string spell(ElementId e, SpellStyle st,
                                  DecisionSource& src) const override {
    using Rows = lex_rows<C, typename model::positions_for<C>::type>;
    if (e >= Rows::kSpell.size() || Rows::kSpell[e] == nullptr) return {};
    std::string out;
    if (!Rows::kSpell[e](out, st, src)) return {};
    return out;
  }

  // Case-folded on both sides: a case-insensitive keyword reserves every
  // spelling of itself, and over-refusing costs one more attempt.
  [[nodiscard]] bool reserved(std::string_view s) const override {
    return std::binary_search(_reserved.begin(), _reserved.end(), folded(s));
  }

  [[nodiscard]] std::string_view separator() const override { return _sep; }

  [[nodiscard]] bool glues(ElementId e) const override {
    using Rows = lex_rows<C, typename model::positions_for<C>::type>;
    return e < Rows::kGlue.size() && Rows::kGlue[e];
  }

  [[nodiscard]] bool devours(ElementId e) const override {
    return e < _devours.size() && _devours[e];
  }

 private:
  [[nodiscard]] static std::string folded(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = to_lower(c);
    return out;
  }

  std::string _sep;
  std::vector<bool> _devours;
  std::vector<std::string> _reserved;
};

}  // namespace cursiv::fuzzer
