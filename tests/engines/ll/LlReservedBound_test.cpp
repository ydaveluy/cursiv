// A name refusing words a terminal ranked below it reads before some bytes
// only: the LR lexer against one reading the grammar's rules by hand, in
// every mode and at every position of every text a small alphabet spells,
// and both engines' parses against each other on the same texts.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "boundasciilr/generated/AsciiLr/Module.hpp"
#include "boundasciilr/generated/AsciiLr/Parser.hpp"
#include "boundasciilr/generated/AsciiLr/Table.hpp"
#include "boundasidelr/generated/AsideLr/Module.hpp"
#include "boundasidelr/generated/AsideLr/Parser.hpp"
#include "boundasidelr/generated/AsideLr/Table.hpp"
#include "boundfrontierlr/generated/FrontierLr/Module.hpp"
#include "boundfrontierlr/generated/FrontierLr/Parser.hpp"
#include "boundfrontierlr/generated/FrontierLr/Table.hpp"
#include "boundwidell/generated/WideLl/Module.hpp"
#include "boundwidelr/generated/WideLr/Module.hpp"
#include "boundwidelr/generated/WideLr/Parser.hpp"
#include "boundwidelr/generated/WideLr/Table.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ReservedBoundAst.hpp"

namespace {

using cursiv::Scan;
using cursiv::TextOffset;
namespace gen = tllbound::generated;

// Bit `n` set: the body reads `n` bytes from the position.
using Ends = std::uint64_t;
using Text = std::string_view;

// A terminal as its rule spells it: the lengths its body reads, whether its
// frontier refuses the byte behind, and whether its frontier is the word
// class -- a keyword, which reserves its spelling from the names.
struct Spelled {
  Text name;
  Ends (*ends)(Text, std::size_t);
  bool (*refuses)(Text, std::size_t);
  bool keyword;
};

struct Lexicon {
  std::vector<Spelled> terms;
  bool (*word)(Text, std::size_t);
};

bool never(Text, std::size_t) { return false; }

bool ascii_byte(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}
bool ascii_word(Text s, std::size_t at) {
  return ascii_byte(static_cast<unsigned char>(s[at]));
}
bool dollar_word(Text s, std::size_t at) {
  return ascii_word(s, at) || s[at] == '$';
}

// Every length a run of `in` reads, from one byte on.
template <class In>
Ends run(Text s, std::size_t from, In in) {
  Ends out = 0;
  for (std::size_t n = 1; from + n <= s.size() && in(s[from + n - 1]); ++n)
    out |= Ends{1} << n;
  return out;
}

Ends literal(Text s, std::size_t from, Text lit) {
  return s.substr(from).starts_with(lit) ? Ends{1} << lit.size() : 0;
}

Ends spaces(Text s, std::size_t from) {
  return run(s, from, [](char c) { return c == ' '; });
}
Ends semi(Text s, std::size_t from) { return literal(s, from, ";"); }

// `kw<word>` at `from` under the boundary `word`.
template <bool (*Word)(Text, std::size_t)>
bool keyword_at(Text s, std::size_t from, Text w) {
  const std::size_t end = from + w.size();
  return s.substr(from).starts_with(w) && (end >= s.size() || !Word(s, end));
}

template <bool (*Word)(Text, std::size_t)>
bool refuses_word(Text s, std::size_t at) {
  return Word(s, at);
}

Ends boolean(Text s, std::size_t from) {
  return literal(s, from, "true") | literal(s, from, "false");
}

// `[a-zA-Z_][a-zA-Z0-9_]*`.
Ends ascii_chars(Text s, std::size_t from) {
  const auto head = [](char c) {
    return ascii_byte(static_cast<unsigned char>(c)) && !(c >= '0' && c <= '9');
  };
  if (from >= s.size() || !head(s[from])) return 0;
  return (run(s, from + 1,
              [](char c) { return ascii_byte(static_cast<unsigned char>(c)); })
          << 1) |
         Ends{2};
}

// The scalar at `p` as the alphabet spells it: ASCII, or `\xC3` and a
// continuation byte; its width, and whether it is a letter or a digit.
struct Scalar {
  std::size_t width;
  bool letter, digit;
};
Scalar scalar_at(Text s, std::size_t p) {
  const auto c = static_cast<unsigned char>(s[p]);
  if (c < 0x80)
    return {1, (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'),
            c >= '0' && c <= '9'};
  if (c == 0xC3 && p + 1 < s.size()) {
    const auto d = static_cast<unsigned char>(s[p + 1]);
    if (d >= 0x80 && d <= 0xBF) {
      const unsigned cp = 0xC0u + (d & 0x3Fu);
      return {2, cp != 0xD7 && cp != 0xF7, false};
    }
  }
  return {0, false, false};
}

// A word character of the wide boundary: ASCII, or a well-formed scalar -- a
// byte that starts none is no character of it.
bool scalar_word(Text s, std::size_t at) {
  return ascii_word(s, at) || (static_cast<unsigned char>(s[at]) >= 0x80 &&
                               scalar_at(s, at).width != 0);
}

// `(uc::L | _) (uc::L | uc::Nd | _)*`, scalar by scalar.
Ends wide_chars(Text s, std::size_t from) {
  Ends out = 0;
  for (std::size_t p = from; p < s.size();) {
    const Scalar c = scalar_at(s, p);
    const bool ok = s[p] == '_' || c.letter || (p != from && c.digit);
    if (c.width == 0 || !ok) break;
    p += c.width;
    out |= Ends{1} << (p - from);
  }
  return out;
}

template <bool (*Word)(Text, std::size_t), Ends (*Chars)(Text, std::size_t)>
Ends refusing_name(Text s, std::size_t from) {
  if (keyword_at<Word>(s, from, "true") || keyword_at<Word>(s, from, "false"))
    return 0;
  return Chars(s, from);
}

template <bool (*Word)(Text, std::size_t), Ends (*Chars)(Text, std::size_t)>
Lexicon boolean_lexicon() {
  return {{{"WS", spaces, never, false},
           {";", semi, never, false},
           {"NAME", refusing_name<Word, Chars>, never, false},
           {"BOOL", boolean, refuses_word<Word>, true}},
          Word};
}

// `[a-z][a-zA-Z0-9_]*` refusing `do`, and `di` where `Both`.
template <bool (*Word)(Text, std::size_t), bool Both>
Ends d_name(Text s, std::size_t from) {
  if (keyword_at<Word>(s, from, "do") ||
      (Both && keyword_at<Word>(s, from, "di")))
    return 0;
  if (from >= s.size() || s[from] < 'a' || s[from] > 'z') return 0;
  return (run(s, from + 1,
              [](char c) { return ascii_byte(static_cast<unsigned char>(c)); })
          << 1) |
         Ends{2};
}
Ends d(Text s, std::size_t from) { return literal(s, from, "d"); }
Ends d_o(Text s, std::size_t from) { return literal(s, from, "do"); }
Ends d_or_e(Text s, std::size_t from) {
  return from < s.size() && (s[from] == 'd' || s[from] == 'e') ? Ends{2} : 0;
}
template <char C>
bool refuses_byte(Text s, std::size_t at) {
  return s[at] == C;
}

Lexicon frontier_lexicon() {
  return {{{"WS", spaces, never, false},
           {";", semi, never, false},
           {"NAME", d_name<dollar_word, true>, never, false},
           {"ONE", d, refuses_byte<'o'>, false},
           {"TWO", d_o, refuses_byte<'!'>, false}},
          dollar_word};
}

Lexicon aside_lexicon() {
  return {{{"WS", spaces, never, false},
           {";", semi, never, false},
           {"NAME", d_name<ascii_word, false>, never, false},
           {"ONE", d, refuses_byte<'!'>, false},
           {"TWO", d_or_e, never, false}},
          ascii_word};
}

// The longest match some terminal of the mode stands at, the best ranked
// there: a frontier refusing the byte behind takes the match away, and where
// no word follows, a keyword reading the spelling takes it from the names.
template <const auto& A>
Scan expected(const Lexicon& lx, const std::vector<const Spelled*>& of,
              const std::vector<char>& in, Text s, std::size_t from) {
  const unsigned nt = A.cfg.nterms;
  std::vector<Ends> ends(nt, 0);
  for (unsigned t = 1; t < nt; ++t)
    if (in[t] != 0) ends[t] = of[t]->ends(s, from);
  const auto literal = [&](unsigned t) {
    return A.cfg.terms[t].spelled && !of[t]->keyword;
  };
  const auto before = [&](unsigned a, unsigned b) {
    const auto rank = [&](unsigned t) {
      return cursiv::lex_rank(of[t]->keyword ? cursiv::kTierKeyword
                              : literal(t)   ? cursiv::kTierLiteral
                                             : 0u,
                              A.cfg.terms[t].trivia);
    };
    return rank(a) != rank(b) ? rank(a) > rank(b) : a < b;
  };
  for (std::size_t n = s.size() - from; n >= 1; --n) {
    const std::size_t at = from + n;
    const Ends bit = Ends{1} << n;
    bool reserves = false, spelling = false;
    for (unsigned t = 1; t < nt; ++t) {
      reserves = reserves || ((ends[t] & bit) != 0 && of[t]->keyword);
      spelling = spelling || ((ends[t] & bit) != 0 && literal(t));
    }
    const bool open = at >= s.size() || !lx.word(s, at);
    int best = -1;
    for (unsigned t = 1; t < nt; ++t) {
      if ((ends[t] & bit) == 0) continue;
      if (at < s.size() && of[t]->refuses(s, at)) continue;
      if (((open && reserves) || spelling) && !A.cfg.terms[t].spelled &&
          !of[t]->keyword)
        continue;
      if (best < 0 || before(t, static_cast<unsigned>(best)))
        best = static_cast<int>(t);
    }
    if (best >= 0)
      return Scan{static_cast<TextOffset>(at),
                  static_cast<cursiv::SymbolId>(best)};
  }
  return Scan{static_cast<TextOffset>(from), cursiv::kInvalidSymbol};
}

std::string shape(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllbound::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || d == nullptr) return "<refused>";
  std::string out;
  for (const tllbound::Item* i : d->items)
    out += "n:" + std::string(i->name) + ' ';
  for (const tllbound::Flag* f : d->flags) out += f->value ? "b:1 " : "b:0 ";
  for (const tllbound::Mark* m : d->marks)
    out += "m:" + std::string(m->one) + '/' + std::string(m->two) + ' ';
  return out;
}

// Exhaust short texts, then focus the longer cases on every keyword prefix,
// behind a lead and before up to three more bytes. This retains the full
// boundary neighborhoods without enumerating long unrelated noise strings.
template <class F>
void every_text(Text alphabet, std::size_t longest,
                std::initializer_list<Text> words, F&& each) {
  std::string text;
  const auto grow = [&](auto&& self, std::size_t left) -> void {
    each(Text{text});
    if (left == 0) return;
    for (const char c : alphabet) {
      text.push_back(c);
      self(self, left - 1);
      text.pop_back();
    }
  };
  grow(grow, longest);
  for (const Text lead : {Text{}, Text{" "}, Text{"x"}})
    for (const Text w : words)
      for (std::size_t k = 1; k <= w.size(); ++k) {
        text = std::string(lead) + std::string(w.substr(0, k));
        grow(grow, 3);
      }
}

template <const auto& A>
struct Lane {
  const char* lang;
  const Lexicon& lx;
  const cursiv::LrDrive& drive;
  std::vector<const Spelled*> of;
  std::vector<std::vector<char>> modes;
  std::vector<char> used;
  std::size_t bad = 0, asked = 0;

  Lane(const char* l, const Lexicon& x, const cursiv::LrDrive& d)
      : lang(l), lx(x), drive(d), of(A.cfg.nterms, nullptr) {
    for (unsigned t = 1; t < A.cfg.nterms; ++t) {
      for (const Spelled& sp : lx.terms)
        if (sp.name == A.cfg.term_name(static_cast<std::uint16_t>(t)))
          of[t] = &sp;
      CHECK(of[t] != nullptr);
    }
    // What each mode reads: its parser states' terminals and the trivia; the
    // error mode reads them all.
    modes.assign(A.modeStart.size(), std::vector<char>(A.cfg.nterms, 0));
    used.assign(A.modeStart.size(), 0);
    for (unsigned st = 0; st < A.nstates; ++st) {
      used[A.mode[st]] = 1;
      for (unsigned t = 1; t < A.cfg.nterms; ++t)
        if (A.cold.follow[st].test(t) || A.cfg.terms[t].trivia)
          modes[A.mode[st]][t] = 1;
    }
    used[A.errMode] = 1;
    for (unsigned t = 1; t < A.cfg.nterms; ++t) modes[A.errMode][t] = 1;
  }

  void read(Text s) {
    for (unsigned m = 0; m < modes.size(); ++m) {
      if (used[m] == 0) continue;
      for (std::size_t from = 0; from <= s.size(); ++from) {
        ++asked;
        const auto len = static_cast<TextOffset>(s.size());
        const auto mode = static_cast<std::uint16_t>(m);
        const Scan want = expected<A>(lx, of, modes[m], s, from);
        const Scan got = cursiv::lr_match_text<A>(
            s.data(), len, mode, static_cast<TextOffset>(from));
        const Scan walk =
            drive.walk(s.data(), len, mode, static_cast<TextOffset>(from));
        const auto same = [&](Scan x) {
          return x.end == want.end && x.term == want.term;
        };
        if (same(got) && same(walk)) continue;
        if (bad++ < 5) {
          std::printf("FAIL %s mode %u at %zu of '", lang, m, from);
          for (const char c : s)
            std::printf(c >= 0x20 && c < 0x7f ? "%c" : "\\x%02x",
                        static_cast<unsigned char>(c));
          std::printf("': want %u/%u, lexer %u/%u, drive %u/%u\n", want.end,
                      want.term, got.end, got.term, walk.end, walk.term);
        }
      }
    }
  }
};

template <const auto& A, class Ll, class Lr>
void agree(const char* lang, const Lexicon& lx, const cursiv::LrDrive& drive,
           const Ll& ll, const Lr& lr, Text alphabet, std::size_t longest,
           std::initializer_list<Text> words) {
  Lane<A> lane(lang, lx, drive);
  std::size_t parses = 0, differ = 0;
  every_text(alphabet, longest, words, [&](Text s) {
    lane.read(s);
    ++parses;
    const std::string b = shape(lr->parse(s));
    const std::string a = ll ? shape(ll->parse(s)) : std::string{};
    if (ll && a != b && differ++ < 5)
      std::printf("FAIL %s parses '%.*s': ll %s, lr %s\n", lang,
                  static_cast<int>(s.size()), s.data(), a.c_str(), b.c_str());
  });
  std::printf("  %-9s positions=%zu bad=%zu parses=%zu differ=%zu\n", lang,
              lane.asked, lane.bad, parses, differ);
  CHECK(lane.bad == 0);
  CHECK(differ == 0);
}

}  // namespace

int main() {
  const Lexicon ascii = boolean_lexicon<dollar_word, ascii_chars>();
  const Lexicon wide = boolean_lexicon<scalar_word, wide_chars>();

  const auto all = [](const auto& ll, const auto& lr,
                      std::initializer_list<std::pair<Text, Text>> cases) {
    for (const auto& [text, want] : cases) {
      if (ll) CHECK(shape(ll->parse(text)) == want);
      CHECK(shape(lr->parse(text)) == want);
    }
  };
  const auto all_ascii =
      std::pair{std::unique_ptr<cursiv::Parser>{}, gen::AsciiLr::create_parser()};
  all(all_ascii.first, all_ascii.second,
      {{"true; x; truex; false;", "n:x n:truex b:1 b:0 "},
       {"true$;", "<refused>"},
       {"tru; t;", "n:tru n:t "}});
  const auto all_wide =
      std::pair{gen::WideLl::create_parser(), gen::WideLr::create_parser()};
  all(all_wide.first, all_wide.second,
      {{"true; trueé; é;", "n:trueé n:é b:1 "},
       {"true\xC3\x97;", "<refused>"}});
  const auto all_frontier = std::pair{std::unique_ptr<cursiv::Parser>{},
                                      gen::FrontierLr::create_parser()};
  all(all_frontier.first, all_frontier.second,
      {{"d; do; dog; d1;", "n:d n:dog n:d1 m:/do "},
       {"do$;", "<refused>"},
       {"do!;", "<refused>"}});
  // Inside the words the rival stands before `i` and not before `o`; past
  // `do` the other one stands before every byte but `!`.
  {
    constexpr const auto& A = gen::FrontierLr::kTable;
    const auto lex = [&](Text text) {
      return cursiv::lr_match_text<A>(
          text.data(), static_cast<TextOffset>(text.size()), A.mode[0], 0);
    };
    const auto term = [&](Text name) {
      for (unsigned t = 1; t < A.cfg.nterms; ++t)
        if (A.cfg.term_name(static_cast<std::uint16_t>(t)) == name)
          return static_cast<cursiv::SymbolId>(t);
      return cursiv::kInvalidSymbol;
    };
    CHECK(lex("di;").end == 1 && lex("di;").term == term("ONE"));
    CHECK(lex("do;").end == 2 && lex("do;").term == term("TWO"));
    CHECK(lex("do!").term == cursiv::kInvalidSymbol);
    CHECK(lex("di$").end == 2 && lex("di$").term == term("NAME"));
  }

  agree<gen::AsciiLr::kTable>(
      "ascii", ascii, gen::AsciiLr::kDrive, std::unique_ptr<cursiv::Parser>{},
      gen::AsciiLr::create_parser(), "true1x$ ;!", 3, {"true", "false"});
  agree<gen::WideLr::kTable>("wide", wide, gen::WideLr::kDrive,
                             gen::WideLl::create_parser(),
                             gen::WideLr::create_parser(),
                             "true1x$ ;\xC3\xA9\x97", 3, {"true", "false"});
  agree<gen::FrontierLr::kTable>(
      "frontier", frontier_lexicon(), gen::FrontierLr::kDrive,
      std::unique_ptr<cursiv::Parser>{}, gen::FrontierLr::create_parser(),
      "doi!$x ;", 3, {"do", "di"});
  agree<gen::AsideLr::kTable>("aside", aside_lexicon(), gen::AsideLr::kDrive,
                              std::unique_ptr<cursiv::Parser>{},
                              gen::AsideLr::create_parser(), "doe!x ;", 3,
                              {"do"});
  return cursiv::test::report("LlReservedBound_test");
}
