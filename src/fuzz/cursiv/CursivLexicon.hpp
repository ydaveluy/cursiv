// The lexicon of one compiled grammar, spelled from its expressions. A
// character range keeps its bytes in its type alone, so the walk is a template.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cursiv/DecisionSource.hpp"
#include "cursiv/DeclaredModel.hpp"
#include "cursiv/FixedSource.hpp"
#include "cursiv/GrammarLexicon.hpp"
#include "cursiv/GrammarModel.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/InGrammar.hpp"
#include "cursiv/grammar/RuleTraits.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/grammar/word_final_in.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/ctstring.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::fuzzer::detail {

// Rule calls a terminal may nest before the walk gives up, and the extra
// rounds a repetition takes.
inline constexpr unsigned kSpellDepth = 6;
inline constexpr std::size_t kSpellRepCap = 6;
// "This expression spells nothing", as a length no sum reaches.
inline constexpr std::size_t kNoSpell = ~std::size_t{0};

// The source plus what the assertion in front forbids. `!cr<R"("\\n)"> + any`
// is the vocabulary's way of writing "any byte but these"; a speller that
// ignored the refusal could close a string literal in its own middle.
struct SpellCtx {
  DecisionSource& src;
  Veto veto{};
  Veto continuation{};

  Veto boundary() const {
    Veto after = veto;
    if (continuation.on) after.forbid(continuation.bytes);
    return after;
  }

  // Whether the next byte spelled may be `b`: the refusal ends there.
  bool admits(char b) {
    const bool ok = !veto.forbids(static_cast<unsigned char>(b));
    veto.clear();
    continuation.clear();
    return ok;
  }
};

// Which byte of a set to spell; the two boundaries are named styles since
// a set is a range and a lexer's off-by-one lives at its ends.
[[nodiscard]] inline bool spell_byte(const cursiv::detail::CharSet& s,
                                     SpellStyle st, SpellCtx& cx,
                                     std::string& out) {
  int lo = -1, hi = -1, nice = -1;
  unsigned count = 0;
  const auto in = [&](unsigned b) {
    const auto c = static_cast<unsigned char>(b);
    return s.test(c) && !cx.veto.forbids(c);
  };
  for (unsigned b = 0; b < 256; ++b) {
    if (!in(b)) continue;
    if (lo < 0) lo = static_cast<int>(b);
    hi = static_cast<int>(b);
    ++count;
    if (nice < 0 && b >= 0x21 && b <= 0x7e) nice = static_cast<int>(b);
  }
  int pick = nice >= 0 ? nice : lo;
  if (lo < 0 || st == SpellStyle::low) {
    pick = lo;
  } else if (st == SpellStyle::high) {
    pick = hi;
  } else if (st == SpellStyle::random) {
    unsigned k = cx.src.choose(DecisionKind::byte, count);
    for (unsigned b = 0; b < 256; ++b)
      if (in(b) && k-- == 0) {
        pick = static_cast<int>(b);
        break;
      }
  }
  if (pick < 0 || !cx.admits(static_cast<char>(pick))) return false;
  out.push_back(static_cast<char>(pick));
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

// The ASCII characters of a class.
template <std::uint64_t M, bool N, str S>
inline constexpr cursiv::detail::CharSet kClassAscii = [] {
  cursiv::detail::CharSet s;
  for (unsigned b = 0; b < 0x80; ++b)
    if (cursiv::detail::word_class<cursiv::detail::CpSet<M, N, S>>::has(b))
      s.set(static_cast<unsigned char>(b));
  return s;
}();
// An unbounded class run takes every following byte in its ASCII set.
template <class E>
inline constexpr const cursiv::detail::CharSet* run_bytes = nullptr;
template <std::size_t Min, std::uint64_t M, bool N, str S>
inline constexpr auto run_bytes<cursiv::detail::Repeat<
    Min, cursiv::detail::kUnbounded, cursiv::detail::CpSet<M, N, S>>> =
    &kClassAscii<M, N, S>;

// The first bytes refusing a class forbids: its ASCII characters, and every
// byte past ASCII when it may hold a scalar there.
template <std::uint64_t M, bool N, str S>
inline constexpr cursiv::detail::CharSet kClassVeto = [] {
  bool beyond = N || M != 0;
  cursiv::detail::for_class_ranges(S.view(), [&beyond](char32_t, char32_t hi) {
    beyond = beyond || hi >= 0x80;
  });
  cursiv::detail::CharSet s = kClassAscii<M, N, S>;
  for (unsigned b = 0x80; beyond && b < 0x100; ++b)
    s.set(static_cast<unsigned char>(b));
  return s;
}();
// The bytes refusing `E` forbids, when `E` is a class.
template <class E>
struct veto_of {
  static constexpr bool any = false;
};
template <std::uint64_t M, bool N, str S>
struct veto_of<cursiv::detail::CpSet<M, N, S>> {
  static constexpr bool any = true;
  static constexpr const cursiv::detail::CharSet& set = kClassVeto<M, N, S>;
};
// Avoiding the first byte of each spelling suffices to satisfy its refusal.
template <str S, bool CI, class Gd, bool K>
struct veto_of<cursiv::detail::Lit<S, CI, Gd, K>> {
  static constexpr bool any = S.size() != 0;
  static constexpr auto set = [] {
    cursiv::detail::CharSet bytes;
    if constexpr (any) {
      const auto first = static_cast<unsigned char>(S[0]);
      bytes.set(first);
      if constexpr (CI) {
        if (first < 0x80)
          cursiv::detail::for_each_other_case(first, [&](char32_t cp) { bytes.set(cp); });
        else
          for (unsigned b = 0xc2; b <= 0xf4; ++b) bytes.set(b);
      }
    }
    return bytes;
  }();
};

template <class C, class E, unsigned D>
struct SpellOf;

// The recursion's entry: an expression reaches the table as the author's
// `static constexpr auto`, so the reference and the const come off here.
template <class C, class E, unsigned D>
struct Spell : SpellOf<C, std::remove_cvref_t<E>, D> {};

// Where an expression can begin and end, in bytes, with nullability so a
// sequence knows how far the two sets reach.
struct Reach {
  cursiv::detail::CharSet first{};
  cursiv::detail::CharSet last{};
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

constexpr Reach reach_one(const cursiv::detail::CharSet& s) {
  return Reach{s, s, false};
}

// Every byte, as `any` accepts: only the walk keeps to printable bytes.
inline constexpr cursiv::detail::CharSet kEveryByte = [] {
  cursiv::detail::CharSet s;
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

// What a literal's guard asserts after its text: a refusal as written, the
// grammar's word boundary where the word ends in it, and any other guard as a
// lookahead, which the walk cannot honour.
template <class C, str S, bool CI, class Gd>
struct lit_guard {
  using type = cursiv::detail::And<Gd>;
};
template <class C, str S, bool CI, class E>
struct lit_guard<C, S, CI, cursiv::detail::Not<E>> {
  using type = cursiv::detail::Not<E>;
};
template <class C, str S, bool CI>
struct lit_guard<C, S, CI, void> {
  using type = cursiv::detail::Epsilon;
};
template <class C, str S, bool CI>
struct lit_guard<C, S, CI, cursiv::detail::WordBound>
    : lit_guard<C, S, CI,
                typename cursiv::detail::scoped_guard<
                    C, S, CI, cursiv::detail::WordBound>::type> {};

template <class C, str S, bool CI, class Gd, bool Keyword, unsigned D>
struct SpellOf<C, cursiv::detail::Lit<S, CI, Gd, Keyword>, D> {
  static constexpr std::size_t min = S.size();
  static constexpr Reach reach() {
    cursiv::detail::CharSet f, l;
    f.set(static_cast<unsigned char>(S[0]));
    l.set(static_cast<unsigned char>(S[S.size() - 1]));
    return Reach{f, l, false};
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    if (!cx.admits(S[0])) return false;
    out += S.view();
    return Spell<C, typename lit_guard<C, S, CI, Gd>::type, D>::run(out, st,
                                                                    cx);
  }
};

// Prose spells nothing: the word under it is what the walk writes.
template <class C, class E, str T, unsigned D>
struct SpellOf<C, cursiv::detail::Doc<E, T>, D> : SpellOf<C, E, D> {};

// A class is spelled in ASCII where it holds any, else by its first scalar.
template <class C, std::uint64_t M, bool N, str S, unsigned D>
struct SpellOf<C, cursiv::detail::CpSet<M, N, S>, D> {
  static constexpr bool ascii = kClassAscii<M, N, S>.count() != 0;
  static constexpr std::size_t min = ascii ? 1 : 2;
  static constexpr Reach reach() {
    cursiv::detail::CharSet s = kClassAscii<M, N, S>;
    if (!ascii)
      for (unsigned b = 0xC2; b <= 0xF4; ++b)
        s.set(static_cast<unsigned char>(b));
    return reach_one(s);
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    if (ascii) return spell_byte(kClassAscii<M, N, S>, st, cx, out);
    for (char32_t cp = 0x80; cp <= 0x10FFFF; ++cp)
      if (cursiv::detail::word_class<cursiv::detail::CpSet<M, N, S>>::has(cp)) {
        const std::size_t at = out.size();
        appendScalar(out, cp);
        return cx.admits(out[at]);
      }
    return false;
  }
};

// `any` is every byte; spelling it outside the printable range would put
// control bytes in a document nothing asked to test.
inline constexpr cursiv::detail::CharSet kPrintable = [] {
  cursiv::detail::CharSet s;
  for (unsigned b = 0x21; b <= 0x7e; ++b) s.set(static_cast<unsigned char>(b));
  return s;
}();

template <class C, unsigned D>
struct SpellOf<C, cursiv::detail::Byte, D> {
  static constexpr std::size_t min = 1;
  static constexpr Reach reach() { return reach_one(kEveryByte); }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return spell_byte(kPrintable, st, cx, out);
  }
};

template <class C, unsigned D>
struct SpellOf<C, cursiv::detail::AnyChar, D>
    : SpellOf<C, cursiv::detail::Byte, D> {};

template <class C, unsigned D>
struct SpellOf<C, cursiv::detail::Epsilon, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, SpellCtx&) { return true; }
};

template <class C, class... Es, unsigned D>
struct SpellOf<C, cursiv::detail::Seq<Es...>, D> {
  static constexpr std::size_t min = spell_sum({Spell<C, Es, D>::min...});
  static constexpr Reach reach() {
    return reach_seq({Spell<C, Es, D>::reach()...});
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return (Spell<C, Es, D>::run(out, st, cx) && ...);
  }
};

template <class C, class... Es, unsigned D>
struct SpellOf<C, cursiv::detail::Unordered<Es...>, D> {
  static constexpr std::size_t min = spell_sum({Spell<C, Es, D>::min...});
  static constexpr Reach reach() {
    return reach_alt({Spell<C, Es, D>::reach()...});
  }
  static bool run(std::string& out, SpellStyle st, SpellCtx& cx) {
    return (Spell<C, Es, D>::run(out, st, cx) && ...);
  }
};

template <class C, class... Es, unsigned D>
struct SpellOf<C, cursiv::detail::Choice<Es...>, D> {
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
struct SpellOf<C, cursiv::detail::Repeat<Mn, Mx, E>, D> {
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
    if (const auto* bytes = run_bytes<cursiv::detail::Repeat<Mn, Mx, E>>)
      cx.continuation.forbid(*bytes);
    return true;
  }
};

// A refusal can name a class, a literal, a choice or a terminal alias.
template <class C, class E, unsigned D>
struct vetoed_by : veto_of<E> {};
template <class C, class R, unsigned D>
struct vetoed_by<C, cursiv::detail::Ref<R>, D>
    : vetoed_by<C,
                std::remove_cvref_t<
                    typename cursiv::detail::Normalize<C>::template body<R>>,
                D - 1> {};
template <class C, class R>
struct vetoed_by<C, cursiv::detail::Ref<R>, 0> : veto_of<void> {};
template <class C, class... Es, unsigned D>
struct vetoed_by<C, cursiv::detail::Choice<Es...>, D> {
  static constexpr bool any = (vetoed_by<C, Es, D>::any && ...);
  static constexpr auto set = [] {
    cursiv::detail::CharSet bytes;
    if constexpr (any) (bytes.merge(vetoed_by<C, Es, D>::set), ...);
    return bytes;
  }();
};

// A negative assertion constrains the next byte without consuming it.
template <class C, class E, unsigned D>
struct SpellOf<C, cursiv::detail::Not<E>, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, [[maybe_unused]] SpellCtx& cx) {
    using Veto = vetoed_by<C, std::remove_cvref_t<E>, D>;
    if constexpr (Veto::any) {
      cx.veto.forbid(Veto::set);
      return true;
    } else {
      return false;
    }
  }
};
template <class C, unsigned D>
struct SpellOf<C, cursiv::detail::Eof, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, SpellCtx& cx) {
    cx.veto.forbid(kEveryByte);
    return true;
  }
};
template <class C, class E, unsigned D>
struct SpellOf<C, cursiv::detail::And<E>, D> {
  static constexpr std::size_t min = 0;
  static constexpr Reach reach() { return Reach{{}, {}, true}; }
  static bool run(std::string&, SpellStyle, SpellCtx&) { return false; }
};

// Prefer an explicit closer to consuming the rest of the document.
template <class Cl>
struct closed_by {
  using type = Cl;
};
template <class L>
struct closed_by<cursiv::detail::Choice<L, cursiv::detail::Eof>> {
  using type = L;
};
template <class L0, class L1, class L2, class... Ls>
struct closed_by<cursiv::detail::Choice<L0, L1, L2, Ls...>> {
  using rest = cursiv::detail::Choice<L1, L2, Ls...>;
  using type = cursiv::detail::cond_t<
      std::is_same_v<typename closed_by<rest>::type, rest>,
      cursiv::detail::Choice<L0, L1, L2, Ls...>,
      typename cursiv::detail::ChoiceCat<L0,
                                         typename closed_by<rest>::type>::type>;
};

template <class C, class O, class Cl0, unsigned D>
struct SpellOf<C, cursiv::detail::Through<O, Cl0>, D> {
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
struct SpellOf<C, cursiv::detail::Ref<R>, D>
    : SpellDeeper<C, typename cursiv::detail::Normalize<C>::template body<R>,
                  D> {};

template <class C, class I, class H, class E, unsigned D>
struct SpellOf<C, cursiv::detail::Skip<I, H, E>, D> : Spell<C, E, D> {};
template <class C, auto Sp, class E, unsigned D>
struct SpellOf<C, cursiv::detail::Assign<Sp, E>, D> : Spell<C, E, D> {};
template <class C, auto Sp, class E, unsigned D>
struct SpellOf<C, cursiv::detail::Append<Sp, E>, D> : Spell<C, E, D> {};
template <class C, auto P, class E, unsigned D>
struct SpellOf<C, cursiv::detail::Flag<P, E>, D> : Spell<C, E, D> {};
template <class C, class T, class E, unsigned D>
struct SpellOf<C, cursiv::detail::Create<T, E>, D> : Spell<C, E, D> {};
template <class C, class T, auto P, class E, unsigned D>
struct SpellOf<C, cursiv::detail::Nest<T, P, E>, D> : Spell<C, E, D> {};

// Spell an optional final line ending explicitly so that the next token
// cannot become part of this line. The raw speller still admits the EOF form.
template <class C, class E>
struct close_line {
  static void finish(std::string&, SpellCtx&) {}
};
template <class C, class... Es>
struct close_line<C, cursiv::detail::Seq<Es...>>
    : close_line<C, cursiv::detail::pack_at_t<sizeof...(Es) - 1, Es...>> {};
template <class C, class E>
struct close_line<C, cursiv::detail::Repeat<0, 1, E>> {
  static void finish(std::string& out, SpellCtx& cx) {
    using End = Spell<C, E, kSpellDepth>;
    constexpr bool line = [] {
      const Reach r = End::reach();
      auto bytes = r.first | r.last;
      if (r.nullable || (!bytes.test('\n') && !bytes.test('\r'))) return false;
      bytes.set('\n');
      bytes.set('\r');
      return bytes.count() == 2;
    }();
    if constexpr (line) {
      if (out.empty() || out.back() == '\n' || out.back() == '\r') return;
      SpellCtx end{cx.src, cx.veto};
      std::string word;
      if (End::run(word, SpellStyle::shortest, end) &&
          (word == "\n" || word == "\r" || word == "\r\n")) {
        out += word;
        cx.veto = end.veto;
        cx.continuation = end.continuation;
      }
    }
  }
};

inline bool all_bytes(std::string_view word, const cursiv::detail::CharSet& bytes) {
  return std::ranges::all_of(word, [&](unsigned char c) { return bytes.test(c); });
}

// Veto a byte when it would extend this word to a longer match.
template <class E>
void guard_extension(E, std::string_view, Veto&) {}
template <std::size_t Min, std::uint64_t M, bool N, str S>
void guard_extension(cursiv::detail::Repeat<Min, cursiv::detail::kUnbounded,
                                            cursiv::detail::CpSet<M, N, S>>,
                     std::string_view word, Veto& after) {
  const auto& bytes = kClassAscii<M, N, S>;
  if (word.size() >= Min && all_bytes(word, bytes)) after.forbid(bytes);
}
template <str S, bool CI, bool K>
void guard_extension(cursiv::detail::Lit<S, CI, void, K>, std::string_view word,
                     Veto& after) {
  if (S.size() == word.size() + 1 && S.view().starts_with(word)) {
    after.bytes.set(static_cast<unsigned char>(S[word.size()]));
    after.on = true;
  }
}
template <class... Es>
void guard_extension(cursiv::detail::Choice<Es...>, std::string_view word,
                     Veto& after) {
  (guard_extension(Es{}, word, after), ...);
}

// One token's spelling, and in `after` what its last assertion forbids the
// byte after it.
template <class C, class E>
bool spell_token(std::string& out, SpellStyle st, DecisionSource& src,
                 Veto& after) {
  SpellCtx cx{src};
  const bool spelled = Spell<C, E, kSpellDepth>::run(out, st, cx);
  if (spelled) {
    close_line<C, std::remove_cvref_t<E>>::finish(out, cx);
    guard_extension(std::remove_cvref_t<E>{}, out, cx.veto);
  }
  after = cx.boundary();
  return spelled;
}

template <class C, class E>
constexpr Reach reach_token() {
  return Spell<C, E, kSpellDepth>::reach();
}

// A trivia rule's shortest word, and the bytes the rule's words end on: a
// token that begins on one of them may be read as more of that trivia.
struct Trivia {
  std::string word;
  cursiv::detail::CharSet ends;
  const cursiv::detail::CharSet* run;
  Veto after;
};

using SpellFn = bool (*)(std::string&, SpellStyle, DecisionSource&, Veto&);
using ReachFn = Reach (*)();
using TriviaFn = std::vector<Trivia> (*)();

template <class C, class E>
consteval SpellFn spell_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (model::kind_of<T>() == ElementKind::terminal_rule)
    return &spell_token<
        C, typename cursiv::detail::Normalize<C>::template body<T>>;
  else if constexpr (model::kind_of<T>() == ElementKind::character_range ||
                     model::kind_of<T>() == ElementKind::any_character ||
                     model::kind_of<T>() == ElementKind::literal ||
                     model::kind_of<T>() == ElementKind::through)
    return &spell_token<C, T>;
  else
    return nullptr;
}

// A function: the bytes a terminal or a `through` begins and ends on, which
// decide the trivia beside it.
template <class C, class E>
consteval ReachFn reach_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (model::kind_of<T>() == ElementKind::terminal_rule)
    return &reach_token<
        C, typename cursiv::detail::Normalize<C>::template body<T>>;
  else if constexpr (model::kind_of<T>() == ElementKind::through ||
                     model::kind_of<T>() == ElementKind::character_range ||
                     model::kind_of<T>() == ElementKind::any_character)
    return &reach_token<C, T>;
  else
    return nullptr;
}

// Each rule of a list of trivia rules that spells a word, in order.
template <class C, class L>
struct lex_trivia;
template <class C, class... Rs>
struct lex_trivia<C, TypeList<Rs...>> {
  static std::vector<Trivia> of() {
    std::vector<Trivia> out;
    if constexpr (sizeof...(Rs) != 0)
      for (const bool line : {false, true})
        (add<typename cursiv::detail::Normalize<C>::template body<Rs>>(out, line), ...);
    return out;
  }
  template <class B>
  static void add(std::vector<Trivia>& out, bool line) {
    FixedSource none;
    SpellCtx cx{none};
    if (line) {
      constexpr auto breaks = kClassAscii<0, false, R"(\r\n)">;
      cx.veto.forbid(~breaks);
    }
    Trivia t{{}, Spell<C, B, kSpellDepth>::reach().last,
             run_bytes<std::remove_cvref_t<B>>, {}};
    if (Spell<C, B, kSpellDepth>::run(t.word, SpellStyle::shortest, cx) &&
        !t.word.empty() && (!line || t.word == "\n" || t.word == "\r" || t.word == "\r\n")) {
      t.after = cx.boundary();
      out.push_back(std::move(t));
    }
  }
};

// A trivia region's trivia, its ignored rules then its hidden ones.
// Substitution has already resolved the region's configuration to rule lists.
template <class C, class E>
inline constexpr TriviaFn region_trivia = nullptr;
template <class C, class I, class H, class E>
inline constexpr TriviaFn region_trivia<C, cursiv::detail::Skip<I, H, E>> =
    &lex_trivia<C, typename cursiv::detail::tl_cat<I, H>::type>::of;

template <class C, class L>
struct lex_rows;
template <class C, class... Es>
struct lex_rows<C, TypeList<Es...>> {
  static constexpr std::array<SpellFn, sizeof...(Es)> kSpell = {
      spell_entry<C, Es>()...};
  static constexpr std::array<TriviaFn, sizeof...(Es)> kTrivia = {
      region_trivia<C, std::remove_cvref_t<Es>>...};
  static constexpr std::array<ReachFn, sizeof...(Es)> kReach = {
      reach_entry<C, Es>()...};
  static constexpr std::array<bool, sizeof...(Es)> kMayCollide = {[] {
    if constexpr (model::kind_of<Es>() == ElementKind::terminal_rule) {
      using Body = typename cursiv::detail::Normalize<C>::template body<Es>;
      return model::text_of<std::remove_cvref_t<Body>>::value.empty();
    }
    return false;
  }()...};
};

template <class C>
struct CursivLexicon final : GrammarLexicon {
  // The literal column comes off the erased model: a spelling the grammar owns
  // is the same whichever path asks for it.
  explicit CursivLexicon(const GrammarModel& g)
      : _trivia(lex_trivia<
                C, typename cursiv::detail::tl_cat<
                       typename cursiv::detail::Normalize<C>::ignored,
                       typename cursiv::detail::Normalize<C>::hidden>::type>::
                    of()) {
    using Rows = lex_rows<C, typename model::positions_for<C>::type>;
    if constexpr (!std::is_void_v<typename C::indent_newline>) {
      _lines.resize(g.positionCount());
      mark_line(C::template ref<typename C::indent_newline>, LineRole::newline);
      mark_line(C::indent_open, LineRole::open);
      mark_line(C::indent_block, LineRole::block);
      mark_line(C::indent_close, LineRole::close);
    }
    _regionTrivia.resize(Rows::kTrivia.size());
    _reads.resize(Rows::kReach.size());
    _first.resize(Rows::kReach.size());
    for (std::size_t e = 0; e < Rows::kReach.size(); ++e) {
      if (Rows::kTrivia[e] != nullptr) _regionTrivia[e] = Rows::kTrivia[e]();
      if (Rows::kReach[e] == nullptr) continue;
      const Reach r = Rows::kReach[e]();
      _reads[e] = r.first | r.last;
      _first[e] = r.first;
    }
    for (ElementId e = 0; e < g.positionCount(); ++e) {
      if (g.kindOf(e) == ElementKind::layout) _usesLayout = true;
      const std::string_view t = g.textOf(e);
      if (t.empty()) continue;
      _reserved.push_back(folded(t));
      if (e < _first.size()) _first[e].set(static_cast<unsigned char>(t[0]));
    }
    std::sort(_reserved.begin(), _reserved.end());
    _reserved.erase(std::unique(_reserved.begin(), _reserved.end()),
                    _reserved.end());
  }

  [[nodiscard]] std::string spell(ElementId e, SpellStyle st,
                                  DecisionSource& src,
                                  Veto& after) const override {
    using Rows = lex_rows<C, typename model::positions_for<C>::type>;
    after = Veto{};
    if (e >= Rows::kSpell.size() || Rows::kSpell[e] == nullptr) return {};
    std::string out;
    if (!Rows::kSpell[e](out, st, src, after)) return {};
    // An explicit literal terminal may own that spelling in its context.
    if (Rows::kMayCollide[e] &&
        std::binary_search(_reserved.begin(), _reserved.end(), folded(out))) return {};
    return out;
  }
  [[nodiscard]] LineRole lineRole(ElementId e) const override {
    return e < _lines.size() ? _lines[e] : LineRole::none;
  }

  [[nodiscard]] std::string_view separator(ElementId scope, ElementId before, ElementId after,
                                           std::string_view previous, char next) const override {
    const auto eol = [](char c) { return c == '\r' || c == '\n'; };
    if (_usesLayout && eol(previous.back()) && !eol(next)) return {};
    const auto reads = [this](ElementId e, std::string_view word) {
      return e < _reads.size() &&
             std::ranges::any_of(word, [&](char c) {
               return _reads[e].test(static_cast<unsigned char>(c));
             });
    };
    const auto runsOn = [this, after](const Trivia& t) {
      if (after >= _first.size()) return false;
      for (unsigned b = 0; b < 256; ++b)
        if (t.ends.test(static_cast<unsigned char>(b)) &&
            _first[after].test(static_cast<unsigned char>(b)))
          return true;
      return false;
    };
    for (const Trivia& t :
         scope < _regionTrivia.size() ? _regionTrivia[scope] : _trivia)
      if (!t.after.forbids(static_cast<unsigned char>(next)) &&
          !reads(before, t.word) && !reads(after, t.word) && !runsOn(t) &&
          !(t.run && all_bytes(previous, *t.run)))
        return t.word;
    return {};
  }

 private:
  template <class E>
  void mark_line(E, LineRole role) {
    if constexpr (!std::is_same_v<E, cursiv::detail::Epsilon>) {
      constexpr ElementId pos = model::op_row<C, E>;
      if constexpr (pos != kNoElement) _lines[pos] = role;
    }
  }
  template <class... Es>
  void mark_line(cursiv::detail::Choice<Es...>, LineRole role) {
    (mark_line(Es{}, role), ...);
  }

  [[nodiscard]] static std::string folded(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
      std::size_t len = 0;
      const char32_t cp = scalarAt(s.data() + i, s.size() - i, len);
      if (len == 0) {
        out += s[i++];
        continue;
      }
      appendScalar(out, cursiv::detail::fold_scalar(cp));
      i += len;
    }
    return out;
  }

  // The grammar's trivia and each region's, then the bytes each terminal or
  // `through` begins or ends on, and the bytes each token begins on.
  std::vector<Trivia> _trivia;
  std::vector<std::vector<Trivia>> _regionTrivia;
  std::vector<cursiv::detail::CharSet> _reads;
  std::vector<cursiv::detail::CharSet> _first;
  std::vector<std::string> _reserved;
  std::vector<LineRole> _lines;
  bool _usesLayout = false;
};

}  // namespace cursiv::fuzzer::detail
