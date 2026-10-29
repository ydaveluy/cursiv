// A relay reads a token's rest past the automaton: an echo ends it at the
// echo of the run its opener chose, whatever the level, and one that never
// comes leaves the opener to the tokens it spells; a nested run ends at the
// closer at depth zero; a relayed run repeated reads every rest once.

#include "cursiv/engines/shared/table/LexRelay.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "backref/generated/G/Module.hpp"
#include "backreflua/generated/G/Module.hpp"
#include "backreflua/generated/G/Table.hpp"
#include "backref/generated/G/Table.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/gen/lr_build.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/runtime/LexAdapter.hpp"
#include "cursiv/engines/shared/runtime/LexMarkRun.hpp"
#include "cursiv/engines/shared/runtime/LexNestedSearch.hpp"
#include "cursiv/engines/shared/runtime/LexRelay.hpp"
#include "cursiv/engines/shared/runtime/lex_find.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/BackrefGrammars.hpp"
#include "engines/lr/LexNestedGrammars.hpp"
#include "nested/generated/G/Module.hpp"
#include "relayoverlap/generated/G/Module.hpp"
#include "nested/generated/G/Table.hpp"

namespace {

// A table's adapters announce what they offer, and the walk refuses one that
// lost it: a skipping adapter without `qualified`, or a relay without its run,
// would otherwise compile and read the other branch.
namespace announced {

using Plain = cursiv::LrLexTape<tnestedlex::generated::G::kTable>;
using Skipping = cursiv::LrLexTape<tnestedlex::generated::G::kTable, true>;

struct LostQualified : Skipping {
  cursiv::SymbolId qualified(std::uint16_t, const char*, cursiv::TextOffset,
                             cursiv::TextOffset,
                             cursiv::TextOffset&) const = delete;
};
struct LostRun : Plain {
  cursiv::TextOffset run(std::uint16_t, const char*, cursiv::TextOffset,
                         cursiv::TextOffset) const = delete;
};
struct LostRelay : Plain {
  std::uint16_t relay(std::uint16_t) const = delete;
};

static_assert(Plain::kRelays && !Plain::kQualified && Skipping::kQualified);
static_assert(cursiv::LexCursor<Plain> && cursiv::LexCursor<Skipping>);
static_assert(cursiv::LexAcceptanceAnnounced<Plain> &&
              cursiv::LexAcceptanceAnnounced<Skipping>);
static_assert(cursiv::LexRelaysAnnounced<Plain> &&
              cursiv::LexRelaysAnnounced<Skipping>);

static_assert(LostQualified::kQualified &&
              cursiv::LexBoundaryFallback<LostQualified> &&
              !cursiv::LexQualifiedAcceptance<LostQualified>);
static_assert(!cursiv::LexAcceptanceAnnounced<LostQualified>);
static_assert(cursiv::LexOffersRelay<LostRun> &&
              !cursiv::LexRelaysAnnounced<LostRun>);
static_assert(LostRelay::kRelays && !cursiv::LexOffersRelay<LostRelay> &&
              !cursiv::LexRelaysAnnounced<LostRelay>);

}  // namespace announced

using Items = std::vector<std::string>;

// The items a clean parse yields, or nothing where the parse complained.
template <class Engine>
bool items(const Engine& engine, std::string_view text, Items& out) {
  const cursiv::ParseResult r = engine->parse(text);
  const auto* const doc = r.value_as<tbackref::ast::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr) return false;
  out.assign(doc->items.begin(), doc->items.end());
  return true;
}

// The same, of a case of the shared table: its opening keyword first.
bool items_of(std::string_view lead, std::string_view text, Items& out) {
  static const auto engine = tbackref::generated::G::create_parser();
  return items(engine, std::string(lead) + " " + std::string(text), out);
}

std::string level(unsigned n, std::string_view body) {
  const std::string eq(n, '=');
  return "[" + eq + "[" + std::string(body) + "]" + eq + "]";
}

void lua_long_brackets() {
  const auto engine = tbackref::lua::generated::G::create_parser();
  Items got;
  for (const auto n : {0u, 1u, 2u, 3u, 10u, 64u, 65u, 126u, 127u, 128u, 1024u}) {
    const std::string one = level(n, " a ]= b ]==x c\n[[ d ");
    CHECK(items(engine, one + " x " + one, got));
    CHECK(got == Items({one, "x", one}));
  }
  // The closer of another level is body; no closer at all is no token, and
  // the opener's bytes are then read as the tokens they spell.
  CHECK(items(engine, "[==[ ]=] ]===] ]==]", got));
  CHECK(got == Items({"[==[ ]=] ]===] ]==]"}));
  CHECK(!items(engine, "[==[ x ]=]", got));
  CHECK(items(engine, "[ ] = x", got));
  CHECK(got == Items({"x"}));
  // The comment holds a long bracket, or a line.
  CHECK(items(engine, "--[==[ two\nlines ]==] x --[[ y ]] z -- [ w\nv", got));
  CHECK(got == Items({"x", "z", "v"}));
  CHECK(!items(engine, "--[==[ open ]=]\nx", got));
  std::printf("  the long bracket closes at its own level\n");
}

void lua_lexes_the_same_from_every_form() {
  namespace g = tbackref::lua::generated::G;
  const std::string text = "[=[ a ]] b ]=]c";
  const auto s = cursiv::lr_match_text<g::kTable>(
      text.data(), static_cast<cursiv::TextOffset>(text.size()), 0, 0);
  CHECK(s.end == 14);
  CHECK(s.term != cursiv::kInvalidSymbol);
  // Cut before the closer, the walk is still alive: the halt says so.
  cursiv::TextOffset halt = 0;
  const auto open = cursiv::lr_match_halt<g::kTable>(
      text.data(), 12, 0, 0, halt);
  CHECK(open.end == 1);
  CHECK(halt == 12);
}

void cpp_raw_strings() {
  Items got;
  CHECK(items_of("cpp", "R\"xy(a\" )x\" )b\")xy\" u8R\"(q)\" \"s\" R", got));
  CHECK(got == Items({"R\"xy(a\" )x\" )b\")xy\"", "u8R\"(q)\"", "\"s\"", "R"}));
  CHECK(!items_of("cpp", "R\"ab(x)ba", got));
  std::printf("  the raw string closes on its own delimiter\n");
}

void twin_echo_outside_through() {
  Items got;
  CHECK(items_of("twin", "<abc-abc> x <z-z>", got));
  CHECK(got == Items({"<abc-abc>", "x", "<z-z>"}));
  CHECK(!items_of("twin", "<abc-abd>", got));
  CHECK(!items_of("twin", "<ab-abc>", got));
  CHECK(!items_of("twin", "<abc-ab>", got));
  const std::string mark(1024, 'a');
  CHECK(items_of("twin", "<" + mark + "-" + mark + ">", got));
  std::printf("  the echo stands where the mark left it\n");
}

void shared_openers() {
  Items got;
  CHECK(items_of("multi", "<abc-abc>! <abc-abc>? <abc-abc>_", got));
  CHECK(got == Items({"<abc-abc>", "<abc-abc>", "<abc-abc>_"}));
  CHECK(items_of("multi", "@abc:bodyabc! @xyz:bodyxyz?", got));
  CHECK(got == Items({"@abc:bodyabc!", "@xyz:bodyxyz?"}));
  CHECK(items_of("multi", "@abc:abc! bodyabc?", got));
  CHECK(got == Items({"@abc:abc! bodyabc?"}));
  CHECK(!items_of("multi", "<abc-abd>!", got));
  CHECK(items_of("multi", "~a! ~aaa! ~aaaaa!", got));
  CHECK(got == Items({"~a!", "~aaa!", "~aaaaa!"}));
  CHECK(!items_of("multi", "~aa!", got));
  for (const unsigned n : {65u, 88u, 128u}) {
    const std::string mark(n, '=');
    const auto text = "[" + mark + "[x0123456789012345678901234567890123456789" + mark + "]";
    CHECK(items_of("multi", text, got));
    CHECK(got == Items({text}));
    CHECK(!items_of("multi", text.substr(0, text.size() - 1), got));
  }
}

// The closer of a heredoc may be indented: the first line break its blanks
// and the tag follow ends it, whatever stood in the lines before.
cursiv::TextOffset heredoc_model(std::string_view t) {
  for (std::size_t i = t.find('\n') + 1; i < t.size(); ++i) {
    if (t[i] != '\n') continue;
    std::size_t e = i + 1;
    while (e < t.size() && (t[e] == ' ' || t[e] == '\t')) ++e;
    if (t.substr(e).starts_with("AB")) return static_cast<cursiv::TextOffset>(e + 2);
  }
  return 0;
}

void an_indented_closer_ends_a_heredoc() {
  Items got;
  CHECK(items_of("heredoc", "<<EOT\nx\nEOT y", got));
  CHECK(got == Items({"<<EOT\nx\nEOT", "y"}));
  CHECK(items_of("heredoc", "<<EOT\n  x\n  EOT y", got));
  CHECK(got == Items({"<<EOT\n  x\n  EOT", "y"}));
  CHECK(items_of("heredoc", "<<EOT\n\tx\n \t EOT", got));
  CHECK(got == Items({"<<EOT\n\tx\n \t EOT"}));
  // A line that only begins like the tag, or holds it past other text, is
  // body; the next line that is the tag closes.
  CHECK(items_of("heredoc", "<<EOT\n  EO\n  x EOT\n EOT", got));
  CHECK(got == Items({"<<EOT\n  EO\n  x EOT\n EOT"}));
  CHECK(items_of("heredoc", "<<EOT\n  \n  \n  EOT", got));
  CHECK(got == Items({"<<EOT\n  \n  \n  EOT"}));
  CHECK(!items_of("heredoc", "<<EOT\n  x\n  EOX", got));
  CHECK(!items_of("heredoc", "<<EOT\nx EOT", got));
  std::mt19937 rng(20260930);
  constexpr std::string_view alphabet = "\n \tABx";
  for (unsigned trial = 0; trial < 4000; ++trial) {
    std::string text = "<<AB\n";
    for (unsigned n = rng() % 16; n != 0; --n)
      text += alphabet[rng() % alphabet.size()];
    const auto end = heredoc_model(text);
    const bool read = items_of("heredoc", text, got);
    CHECK(read == (end != 0));
    if (read) CHECK(got.front() == text.substr(0, end));
  }
  std::printf("  an indented closer ends the heredoc\n");
}

void long_closer_search() {
  for (const std::string_view text : {"", "a", "aba", "ababa", "aaaab"})
    for (const std::string_view pattern : {"", "a", "ab", "aba", "aaaab", "aaaaaa"})
      for (std::size_t first = 0; first <= pattern.size(); ++first)
        for (std::size_t last = first; last <= pattern.size(); ++last) {
          const auto* hit = cursiv::lex_find_long(
              text, pattern.substr(0, first), pattern.substr(first, last - first),
              pattern.substr(last));
          const auto want = text.find(pattern);
          CHECK(hit == (want == std::string_view::npos ? nullptr : text.data() + want));
        }
  std::mt19937 rng(947);
  for (unsigned trial = 0; trial < 1500; ++trial) {
    std::string text(512, 'a'), pattern(130 + rng() % 128, 'a');
    for (auto& c : text) c = static_cast<char>((rng() % 4) * 85);
    for (auto& c : pattern) c = static_cast<char>((rng() % 4) * 85);
    if (trial % 2) text.replace(rng() % 256, pattern.size(), pattern);
    const unsigned at = rng() % pattern.size();
    const unsigned after = at + rng() % (pattern.size() - at);
    const std::string_view p = pattern;
    const auto* hit = cursiv::lex_find_long(text, p.substr(0, at),
                                           p.substr(at, after - at), p.substr(after));
    const auto want = text.find(pattern);
    CHECK(hit == (want == std::string::npos ? nullptr : text.data() + want));
  }
  const std::string mark(4096, 'a');
  const std::string text = std::string(100000, 'a') + 'b';
  CHECK(cursiv::lex_find_long(text, "a", mark, "b") == text.data() + 95903);
  CHECK(cursiv::lex_find_long(text, "a", mark, "c") == nullptr);
}

struct Other {};
template <class Tag>
struct Echo : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct T : terminal<lit<"<"> + mark<tbackref::Level>(some(w)) + lit<"-"> +
                      same<Tag> + lit<">">> {};
  struct Root : rule<create<tbackref::ast::Doc>(
                    many(append<&tbackref::ast::Doc::items>(ref<T>)))> {};
  using entry = Root;
};
template <class Tag>
struct Closer : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct T : terminal<through(
                 lit<"["> + mark<tbackref::Level>(many(cr<"=">)) + lit<"[">,
                 lit<"]"> + same<Tag> + lit<"]">)> {};
  struct Root : rule<create<tbackref::ast::Doc>(
                    many(append<&tbackref::ast::Doc::items>(ref<T>)))> {};
  using entry = Root;
};

// A closer's run of blanks, next to a mark it could swallow, or in front of
// an echo that may be empty with nothing past it to stop the run.
template <class Mark, class Pad, class After>
struct Padded : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct T
      : terminal<through(lit<"<<"> + mark<tbackref::Level>(Mark{}) + lit<"\n">,
                         lit<"\n"> + Pad{} + same<tbackref::Level> + After{})> {
  };
  struct Root : rule<create<tbackref::ast::Doc>(
                    many(append<&tbackref::ast::Doc::items>(ref<T>)))> {};
  using entry = Root;
};
template <class Mark, class Pad>
struct PaddedBare : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct T
      : terminal<through(lit<"<<"> + mark<tbackref::Level>(Mark{}) + lit<"\n">,
                         lit<"\n"> + Pad{} + same<tbackref::Level>)> {};
  struct Root : rule<create<tbackref::ast::Doc>(
                    many(append<&tbackref::ast::Doc::items>(ref<T>)))> {};
  using entry = Root;
};

// A literal one byte longer than a list's length byte counts.
constexpr char kLong[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

// A recursive terminal may use a composed opener and closer.
template <class Open, class Close>
struct Recursive : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct T
      : terminal<Open{} + many(ref<T> | (!Open{} + !Close{} + any)) + Close{}> {
  };
  struct Root : rule<create<tbackref::ast::Doc>(
                    many(append<&tbackref::ast::Doc::items>(ref<T>)))> {};
  using entry = Root;
};

template <class G>
cursiv::LowerFail lowers() {
  return cursiv::lr_build_automaton(cursiv::LrLowering<G>::table())
      .cfg.lowerWhy;
}

// An echo names its mark by the tag, behind the run's fence and in a closer.
void an_echo_under_another_tag_is_refused() {
  CHECK(lowers<Echo<tbackref::Level>>() == cursiv::LowerFail::none);
  CHECK(lowers<Echo<Other>>() == cursiv::LowerFail::echoUnmarked);
  CHECK(lowers<Closer<tbackref::Level>>() == cursiv::LowerFail::none);
  CHECK(lowers<Closer<Other>>() == cursiv::LowerFail::echoUnmarked);
  std::printf("  an echo answers to its own mark alone\n");
}

using Upper = decltype(cursiv::some(cursiv::cr<"A-Z">));
using MaybeUpper = decltype(cursiv::many(cursiv::cr<"A-Z">));
using Blanks = decltype(cursiv::many(cursiv::cr<R"( \t)">));
using Semi = decltype(cursiv::lit<";">);

// The compact relay needs a pad that stops before the echo. The general
// recognizer also admits overlapping classes and an unbounded empty echo.
void a_closer_run_stops_where_the_echo_starts() {
  CHECK(lowers<PaddedBare<Upper, Blanks>>() == cursiv::LowerFail::none);
  CHECK(lowers<Padded<MaybeUpper, Blanks, Semi>>() == cursiv::LowerFail::none);
  CHECK(lowers<PaddedBare<Upper, decltype(cursiv::many(cursiv::cr<" A">))>>() ==
        cursiv::LowerFail::none);
  CHECK(lowers<PaddedBare<MaybeUpper, Blanks>>() == cursiv::LowerFail::none);
  CHECK(lowers<Padded<MaybeUpper, decltype(cursiv::many(cursiv::cr<" ;">)),
                      Semi>>() == cursiv::LowerFail::none);
}

void a_recursive_terminal_accepts_composed_delimiters() {
  using A = decltype(cursiv::lit<"a">);
  using B = decltype(cursiv::lit<"b">);
  CHECK(lowers<Recursive<decltype(A{} | cursiv::lit<"c">), B>>() ==
        cursiv::LowerFail::none);
  CHECK(
      lowers<Recursive<A, decltype(B{} | cursiv::lit<"c"> | cursiv::eof)>>() ==
      cursiv::LowerFail::none);
  CHECK(lowers<Recursive<decltype(A{} | cursiv::cr<"cd">), B>>() ==
        cursiv::LowerFail::none);
  CHECK(lowers<Recursive<decltype(A{} | cursiv::lit<kLong>), B>>() ==
        cursiv::LowerFail::none);
  CHECK(lowers<Recursive<decltype(cursiv::lit<kLong>), B>>() ==
        cursiv::LowerFail::none);
  CHECK(lowers<Recursive<A, decltype(B{} | cursiv::lit<kLong>)>>() ==
        cursiv::LowerFail::none);
  CHECK(lowers<Recursive<A, decltype(cursiv::lit<kLong>)>>() ==
        cursiv::LowerFail::none);
  CHECK(lowers<Recursive<decltype(cursiv::lit<kLong>),
                         decltype(B{} | cursiv::lit<"c">)>>() ==
        cursiv::LowerFail::none);
  CHECK(lowers<Recursive<decltype(A{} | cursiv::lit<"c">),
                         decltype(cursiv::lit<kLong>)>>() ==
        cursiv::LowerFail::none);
}

void nested_delimiter_choices_are_flattened_for_the_native_search() {
  using A = cursiv::Lit<"a">;
  using B = cursiv::Lit<"b">;
  using Open =
      cursiv::Choice<A, cursiv::Choice<cursiv::Lit<"c">, cursiv::Lit<"d">>>;
  using Close =
      cursiv::Choice<B, cursiv::Choice<cursiv::Lit<"e">, cursiv::Lit<"f">>>;
  const auto built = cursiv::lr_build_automaton(
      cursiv::LrLowering<Recursive<Open, Close>>::table());
  CHECK(built.cfg.lowerWhy == cursiv::LowerFail::none);
  CHECK(built.nrelays == 1);
  if (built.nrelays != 1) return;
  const auto& relay = built.relays[0];
  CHECK(relay.nested && relay.several);
  constexpr std::string_view text = "acxebadfb";
  CHECK(cursiv::LexNestedSearch::once(relay, built.cfg.names.data(),
                                      text.data(), text.size(), 1) == 5);
  CHECK(cursiv::LexNestedSearch::once(relay, built.cfg.names.data(),
                                      text.data(), text.size(), 6) == 9);
}

// `any` tests a delimiter at the start of a character only: one starting with
// a UTF-8 continuation byte, which may stand inside a character, is not
// searched for byte by byte.
void a_continuation_byte_delimiter_is_not_searched_natively() {
  using Open = cursiv::Lit<"(">;
  using Close = cursiv::Lit<"\xA9">;
  const auto natively = [](const auto& built) {
    for (std::size_t k = 0; k < built.nrelays; ++k)
      if (built.relays[k].nested) return true;
    return false;
  };
  const auto closing = cursiv::lr_build_automaton(
      cursiv::LrLowering<Recursive<Open, Close>>::table());
  CHECK(closing.cfg.lowerWhy == cursiv::LowerFail::none);
  CHECK(!natively(closing));
  const auto opening = cursiv::lr_build_automaton(
      cursiv::LrLowering<Recursive<Close, Open>>::table());
  CHECK(opening.cfg.lowerWhy == cursiv::LowerFail::none);
  CHECK(!natively(opening));
}

// The items of a clean reading, one space apart; `<refused>` otherwise.
template <class Doc>
std::string joined(const cursiv::Parser& p, const std::string& doc) {
  const cursiv::ParseResult r = p.parse(doc);
  if (!r.accepted || !r.diagnostics.empty()) return "<refused>";
  std::string out;
  for (const auto item : r.value_as<Doc>()->items) {
    if (!out.empty()) out += ' ';
    out += item;
  }
  return out;
}

template <class Doc>
bool reads(const cursiv::Parser& p, const std::string& doc,
           std::string_view want) {
  const std::string got = joined<Doc>(p, doc);
  if (got == want) return true;
  std::printf("  %.*s -> %.*s\n", static_cast<int>(doc.size() > 80 ? 80 : doc.size()),
              doc.data(), static_cast<int>(got.size() > 80 ? 80 : got.size()),
              got.data());
  return false;
}

bool nested(std::string_view text, std::string_view want) {
  static const auto p = tnestedlex::generated::G::create_parser();
  return reads<tnestedlex::ast::Doc>(*p, std::string(text), want);
}

bool repeated(std::string_view text, std::string_view want) {
  static const auto p = tbackref::generated::G::create_parser();
  return reads<tbackref::ast::Doc>(*p, "repeat " + std::string(text), want);
}

std::string times(std::string_view unit, unsigned n) {
  std::string out;
  out.reserve(unit.size() * n);
  for (unsigned k = 0; k < n; ++k) out += unit;
  return out;
}

// A nested run ends at the closer at depth zero, whatever stands inside,
// and never at a closer an inner opener still owes.
void a_nested_run_ends_at_depth_zero() {
  CHECK(nested("a /* x */ b", "a b"));
  CHECK(nested("a /**/ b", "a b"));
  CHECK(nested("a /*/ x */ b", "a b"));
  // One level, three levels, and openers side by side.
  CHECK(nested("a /* /* x */ */ b", "a b"));
  CHECK(nested("a /* /* /* x */ y */ z */ b", "a b"));
  CHECK(nested("a /* /* */ /* */ */ b", "a b"));
  CHECK(nested("a /*/*/**/*/*/ b", "a b"));
  // A closer an inner opener still owes ends nothing.
  CHECK(nested("a /* /* x */ b", "<refused>"));
  CHECK(nested("a /* x", "<refused>"));
  // An opener inside a string outside the comment is text.
  CHECK(nested("a \"/*\" b /* c */ d", "a \"/*\" b d"));
  // A closer inside a string inside the comment still closes: the run is
  // bytes, as `through` reads them, and the rest is a string never closed.
  CHECK(nested("a /* \"*/\" b", "<refused>"));
  CHECK(nested("a /* \"*/\" b */ c", "<refused>"));
  // A newline changes nothing.
  CHECK(nested("a /*\n /*\n */\n */ b", "a b"));
  // One pass over the openers, however many stand before the first closer.
  {
    std::string deep = "a ", unclosed = "a ";
    for (unsigned k = 0; k < 100000; ++k) {
      deep += "/*";
      unclosed += "/* x ";
    }
    for (unsigned k = 0; k < 100000; ++k) deep += "*/";
    CHECK(nested(deep + " b", "a b"));
    CHECK(nested(unclosed + "*/ b", "<refused>"));
  }
  const auto overlap = tnestedlex::overlap::generated::G::create_parser();
  for (const std::string_view text : {"/*x*/", "/*xx*/", "/*x/*y*/z*/", "/*x",
                                      "<*x*>"}) {
    const auto r = overlap->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto* doc = r.value_as<tnestedlex::ast::Doc>();
    CHECK(doc && doc->items.size() == 1 && doc->items[0] == text);
  }
  CHECK(overlap->parse("if!").diagnostics.empty());
  CHECK(overlap->parse("igf?").diagnostics.empty());
  CHECK(!overlap->parse("if?").diagnostics.empty());
}

// Several openers deepen one count and several closers close it; a run no
// closer balances ends with the text where its closer says `eof`.
void a_nested_run_counts_every_opener_it_lists() {
  CHECK(nested("a `ifdef x `endif b", "a b"));
  CHECK(nested("a `ifndef x `endif b", "a b"));
  CHECK(nested("a `ifdef x `ifndef y `endif z `endif b", "a b"));
  CHECK(nested("a `ifndef x `ifdef y `ifndef q `endif `endif z `endif b",
               "a b"));
  CHECK(nested("a `ifdef x `ifndef y `endif z b", "<refused>"));
  CHECK(nested("a `ifdefx `endif b", "a b"));
  CHECK(nested("a {| x (| y |} z |) b", "a b"));
  CHECK(nested("a (| x {| y |) z |} b", "a b"));
  CHECK(nested("a {| x (| y |} b", "<refused>"));
  CHECK(nested("a (* x *) b", "a b"));
  CHECK(nested("a (* x (* y *) z *) b", "a b"));
  CHECK(nested("a (* x (* y *) b", "a"));
  CHECK(nested("a (* x", "a"));
  CHECK(nested("a (*", "a"));
  std::printf("  a nested run counts every opener it lists\n");
}

bool pragma(std::string_view text, std::string_view want) {
  static const auto p = tnestedlex::generated::G::create_parser();
  return reads<tnestedlex::ast::Doc>(*p, std::string(text), want);
}

// A nested comment as the language spells it, and a token's end where the
// text starts one: `{-#` is no comment, a byte after the opener is content.
cursiv::TextOffset pragma_model(std::string_view t, bool dollar = false) {
  if (!t.starts_with("{-")) return 0;
  if (t.starts_with("{-#") || (dollar && t.starts_with("{-$"))) return 3;
  unsigned depth = 1;
  for (std::size_t i = 2; i < t.size();) {
    const auto rest = t.substr(i);
    if (rest.starts_with("{-")) {
      ++depth;
      i += 2;
    } else if (rest.starts_with("-}")) {
      i += 2;
      if (--depth == 0) return static_cast<cursiv::TextOffset>(i);
    } else {
      ++i;
    }
  }
  return 0;
}

// A balanced run's delimiters are as long as the author writes them.
void delimiters_are_as_long_as_written() {
  static const auto p = tnestedlex::generated::G::create_parser();
  const std::string open(40, '<'), close(40, '>');
  CHECK(reads<tnestedlex::ast::Doc>(
      *p, "a " + open + " x " + open + " y " + close + " z " + close + " b",
      "a b"));
  CHECK(reads<tnestedlex::ast::Doc>(*p, "a " + open + " x " + open + " y " + close,
                                 "<refused>"));
  CHECK(reads<tnestedlex::ast::Doc>(*p, "a " + open + open + close + close + " b",
                                 "a b"));
}

// A refusal in front of a nested run ends on the run's first content byte:
// the walk reads it off the refused class and the search starts behind it, so
// a closer or an opener standing on that byte still counts.
void a_refusal_before_a_relay_leaves_its_first_byte_to_the_search() {
  CHECK(pragma("a {- x -} b", "a b"));
  CHECK(pragma("a {--} b", "a b"));
  CHECK(pragma("a {-{- x -}-} b", "a b"));
  CHECK(pragma("a {- {-# x #-} -} b", "a b"));
  CHECK(pragma("a {-\xC3\xA9-} b", "a b"));
  CHECK(pragma("a {-}-} b", "a b"));
  CHECK(pragma("a {-# x #-} b", "a {-# x #-} b"));
  CHECK(pragma("{-#-}", "<refused>"));
  CHECK(pragma("a {-}", "<refused>"));
  CHECK(pragma("a {- x", "<refused>"));
  CHECK(pragma("a {-", "<refused>"));
  CHECK(pragma("a {-# x", "a {-# x"));
  namespace g = tnestedlex::generated::G;
  std::mt19937 rng(20260929);
  constexpr std::string_view alphabet = "{-}#a ";
  for (unsigned trial = 0; trial < 20000; ++trial) {
    std::string text = "{-";
    for (unsigned n = rng() % 12; n != 0; --n)
      text += alphabet[rng() % alphabet.size()];
    const auto s = cursiv::lr_match_text<g::kTable>(
        text.data(), static_cast<cursiv::TextOffset>(text.size()), 0, 0);
    if (s.end != pragma_model(text)) std::printf("  %s\n", text.c_str());
    CHECK(s.end == pragma_model(text));
  }
  namespace two = tnestedlex::generated::G;
  constexpr std::string_view more = "{-}#$a ";
  for (unsigned trial = 0; trial < 20000; ++trial) {
    std::string text = "{-";
    for (unsigned n = rng() % 12; n != 0; --n)
      text += more[rng() % more.size()];
    const auto s = cursiv::lr_match_text<two::kTable>(
        text.data(), static_cast<cursiv::TextOffset>(text.size()), 0, 0);
    if (s.end != pragma_model(text, true)) std::printf("  %s\n", text.c_str());
    CHECK(s.end == pragma_model(text, true));
  }
  std::printf("  the refusal leaves the first byte to the relay\n");
}

// A relayed run repeated: every rest is read once, in a loop, so a token of
// a hundred thousand groups neither exhausts the stack nor re-reads the
// groups behind each opener.
void a_relayed_run_reads_every_rest_once() {
  CHECK(repeated("a:{a}{b}x;", "a:{a}{b}x"));
  CHECK(repeated("a:x{y;", "a:x{y"));
  CHECK(repeated("a:}{;", "a:}{"));
  CHECK(repeated("b:{a}{b}x;", "b:{a}{b}x"));
  CHECK(repeated("b:{a{b}c}d;", "b:{a{b}c}d"));
  CHECK(repeated("b:{a}{b;", "<refused>"));
  CHECK(repeated("c:f(a)(b)(c);", "c:f(a)(b)(c)"));
  CHECK(repeated("c:f(a(b)c)x;", "c:f(a(b)c)x"));
  CHECK(repeated("r:<ab>ab<c>c;", "r:<ab>ab<c>c"));
  CHECK(repeated("r:<ab>ab<c>d;", "<refused>"));
  CHECK(repeated("r:<ab>ac;", "<refused>"));

  // Every opener of the wide run starts a relay whose rest is the run
  // itself: read once per position, forty groups cost forty.
  for (const unsigned n : {40u, 100000u}) {
    const std::string wide = "a:" + times("{a}", n);
    CHECK(repeated(wide + ";", wide));
    const std::string arg = "c:f" + times("(a)", n);
    CHECK(repeated(arg + ";", arg));
  }
  // Openers with no closer, one closer far behind them, or one each: every
  // opener starts a search, and the searches read the text once between them.
  constexpr unsigned kOpeners = 400000;
  const std::string parens = "c:f" + std::string(kOpeners, '(');
  CHECK(repeated(parens + ";", parens));
  CHECK(repeated(parens + ");", parens + ")"));
  const std::string deep = parens + std::string(kOpeners, ')');
  CHECK(repeated(deep + ";", deep));
  const std::string braces = "a:" + std::string(kOpeners, '{');
  CHECK(repeated(braces + ";", braces));
  CHECK(repeated(braces + "};", braces + "}"));
  // An echo whose fence the mark's class holds starts one at each byte of
  // the run, and the token is the longest of them.
  for (const unsigned k : {0u, 1u, 2u, 3u, 7u, 100u, 20001u}) {
    const std::string run(k, 'a');
    const std::string echoed = "f:<" + run + "a" + run;
    CHECK(repeated(echoed + ";", echoed));
    CHECK(repeated(echoed + "a;", "<refused>"));
  }
  // Each repetition hands the rest to the next: a loop, not a frame each.
  const std::string narrow = "b:" + times("{a}", 100000);
  CHECK(repeated(narrow + ";", narrow));
  const std::string twin = "r:" + times("<a>a", 300000);
  CHECK(repeated(twin + ";", twin));
}

// The record of a balanced run: `names` holds the opener, then the closer.
cursiv::LexRelay balanced_of(std::string_view open, std::string_view close) {
  cursiv::LexRelay r;
  r.nested = true;
  r.beforeLen = static_cast<std::uint16_t>(open.size());
  r.afterOff = r.beforeLen;
  r.afterLen = static_cast<std::uint16_t>(close.size());
  return r;
}

// A search that keeps what it settled answers as the search that keeps
// nothing does, from any opener in any order -- literals that overlap
// themselves or each other included -- and reads a text once, where one
// search per opener reads it once per opener.
void a_nested_search_remembers_what_it_settled() {
  constexpr std::string_view kPairs[][2] = {
      {"(", ")"},   {"((", "))"}, {"/*", "*/"}, {"{-", "-}"},
      {"ab", "ba"}, {"a", "b"},   {"aa", "a"},  {"a", "aa"},
      {"<!", "!>"}, {"*", "*"},   {"**", "*"},  {"/*", "/"}};
  std::mt19937 rng(20260929);
  unsigned long asked = 0;
  for (unsigned trial = 0; trial < 60000; ++trial) {
    const auto& [open, close] = kPairs[rng() % std::size(kPairs)];
    const std::string names = std::string(open) + std::string(close);
    const auto r = balanced_of(open, close);
    const std::string alphabet = names + "x/*(){}!<ab-";
    std::string text;
    for (unsigned n = rng() % 40; n != 0; --n)
      text += alphabet[rng() % alphabet.size()];
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    cursiv::LexNestedSearch memo;
    for (unsigned q = 1 + rng() % 30; q != 0; --q, ++asked) {
      const cursiv::TextOffset from = rng() % (len + 1);
      const auto want = cursiv::LexNestedSearch::once(r, names.data(),
                                                      text.data(), len, from);
      const auto got = memo.seek(r, names.data(), text.data(), len, from);
      if (got != want)
        std::printf("  %s %s %s %u: %u, not %u\n", std::string(open).c_str(),
                    std::string(close).c_str(), text.c_str(), from, got, want);
      CHECK(got == want);
    }
  }
  std::printf("  the memory answers as the search does (%lu asked)\n", asked);

  // A million openers, asked one by one: the closers come never, once, or
  // one for each -- and where the opener overlaps itself there are two
  // chains of boundaries in one text.
  constexpr cursiv::TextOffset n = 1000000;
  const std::string opens(n, '(');
  const std::string single = opens + ")";
  const std::string every = opens + std::string(n, ')');
  constexpr std::string_view kLiterals[][2] = {{"(", ")"}, {"((", "))"}};
  for (const auto& [open, close] : kLiterals) {
    const std::string names = std::string(open) + std::string(close);
    const auto r = balanced_of(open, close);
    cursiv::LexNestedSearch none, lone, all;
    for (cursiv::TextOffset from = 1; from <= n; ++from) {
      CHECK(none.seek(r, names.data(), opens.data(), n, from) == 0);
      const auto one = lone.seek(r, names.data(), single.data(), n + 1, from);
      const auto e = all.seek(r, names.data(), every.data(), 2 * n, from);
      if (r.beforeLen == 1) {
        CHECK(one == (from == n ? n + 1 : 0));
        CHECK(e == 2 * n - from + 1);
      } else {
        const cursiv::TextOffset owed = 1 + (n - from) / 2;
        CHECK(one == 0);
        CHECK(e == (owed <= n / 2 ? n + 2 * owed : 0));
      }
    }
  }
  std::printf("  a million openers are read once\n");
}

// The record of a balanced run listing its literals: `names` holds the
// openers, then the closers, each behind a byte of its length.
cursiv::LexRelay listed_of(std::string& names,
                           const std::vector<std::string_view>& open,
                           const std::vector<std::string_view>& close,
                           bool eof) {
  names.clear();
  cursiv::LexRelay r;
  r.nested = true;
  r.several = true;
  r.eof = eof;
  for (const auto lit : open) (names += static_cast<char>(lit.size())) += lit;
  r.beforeLen = static_cast<std::uint16_t>(names.size());
  r.afterOff = r.beforeLen;
  for (const auto lit : close) (names += static_cast<char>(lit.size())) += lit;
  r.afterLen = static_cast<std::uint16_t>(names.size() - r.afterOff);
  return r;
}

// Position by position: the longest closer standing there, else the longest
// opener, else the next byte.
cursiv::TextOffset listed_model(std::string_view t, cursiv::TextOffset from,
                                const std::vector<std::string_view>& open,
                                const std::vector<std::string_view>& close,
                                bool eof) {
  const auto longest = [&](std::size_t i,
                           const std::vector<std::string_view>& lits) {
    std::size_t best = 0;
    for (const auto lit : lits)
      if (lit.size() > best && t.substr(i).starts_with(lit)) best = lit.size();
    return best;
  };
  unsigned depth = 1;
  for (std::size_t i = from; i < t.size();) {
    if (const auto n = longest(i, close)) {
      i += n;
      if (--depth == 0) return static_cast<cursiv::TextOffset>(i);
    } else if (const auto m = longest(i, open)) {
      i += m;
      ++depth;
    } else {
      ++i;
    }
  }
  return eof ? static_cast<cursiv::TextOffset>(t.size()) : 0;
}

// Listed literals, overlapping ones among them, and the end of the text as a
// closer: the search, and the one that keeps what it settled, answer as a
// reading position by position does.
void a_listed_search_reads_as_the_model() {
  struct Sides {
    std::vector<std::string_view> open, close;
  };
  const Sides kSides[] = {{{"`if", "`ifn"}, {"`end"}},
                          {{"(", "[["}, {")", "]]"}},
                          {{"ab", "b"}, {"ba", "a"}},
                          {{"<", "<<"}, {">"}},
                          {{"x"}, {"y", "yy"}}};
  std::mt19937 rng(20260930);
  std::string names;
  for (unsigned trial = 0; trial < 40000; ++trial) {
    const Sides& sides = kSides[rng() % std::size(kSides)];
    const bool eof = rng() % 2 != 0;
    const auto r = listed_of(names, sides.open, sides.close, eof);
    constexpr std::string_view alphabet = "`ifnd()[]abyx<>-";
    std::string text;
    for (unsigned n = rng() % 40; n != 0; --n)
      text += alphabet[rng() % alphabet.size()];
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    cursiv::LexNestedSearch memo;
    for (unsigned q = 1 + rng() % 20; q != 0; --q) {
      const cursiv::TextOffset from = rng() % (len + 1);
      const auto want = listed_model(text, from, sides.open, sides.close, eof);
      const auto once = cursiv::LexNestedSearch::once(r, names.data(),
                                                      text.data(), len, from);
      const auto got = memo.seek(r, names.data(), text.data(), len, from);
      if (once != want || got != want)
        std::printf("  %s %u: %u %u, not %u\n", text.c_str(), from, once, got,
                    want);
      CHECK(once == want);
      CHECK(got == want);
    }
  }
  std::printf("  a listed search reads as the model\n");
}

// The run of a class before a point, read once along the launches of one
// relay, and again where a launch comes from behind.
void a_mark_run_is_read_once() {
  cursiv::ByteSet cls;
  cls.add('a');
  cls.add('b');
  std::mt19937 rng(947);
  for (unsigned trial = 0; trial < 20000; ++trial) {
    std::string text;
    for (unsigned n = rng() % 30; n != 0; --n) text += "abc"[rng() % 3];
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    const cursiv::TextOffset start = rng() % (len + 1);
    cursiv::LexMarkRun run;
    for (unsigned q = 1 + rng() % 20; q != 0; --q) {
      const cursiv::TextOffset end = start + rng() % (len - start + 1);
      const auto want = cursiv::LexMarkRun::back(cls, text.data(), start, end);
      CHECK(run.begin(cls, text.data(), start, end) == want);
    }
  }
  constexpr cursiv::TextOffset n = 1000000;
  std::string text(n, 'a');
  text[n / 2] = 'c';
  cursiv::LexMarkRun run;
  for (cursiv::TextOffset end = 0; end <= n; ++end)
    CHECK(run.begin(cls, text.data(), 0, end) ==
          (end <= n / 2 ? 0 : n / 2 + 1));
  std::printf("  a mark's run is read once\n");
}

// The room a padded closer needs past its run is counted beyond the 32 bits
// of an offset: a mark run near the 4 GiB bound and the bytes after its echo
// do not wrap into a small count that lets the echo compare run past the
// text. The text ends against an unreadable page, so such a read faults.
void a_closer_room_near_the_bound_does_not_wrap() {
#if defined(__unix__) || defined(__APPLE__)
  const long pageSize = sysconf(_SC_PAGESIZE);
  CHECK(pageSize > 0);
  if (pageSize <= 0) return;
  const auto page = static_cast<std::size_t>(pageSize);
  void* region = mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(region != MAP_FAILED);
  if (region == MAP_FAILED) return;
  char* const end = static_cast<char*>(region) + page;
  const bool protectedEnd = mprotect(end, page, PROT_NONE) == 0;
  CHECK(protectedEnd);
  if (protectedEnd) {
    constexpr std::string_view kText = "xxxxxxxxxxxxxxxx";
    std::memcpy(end - kText.size(), kText.data(), kText.size());
    // `before` is "x", `after` sixteen bytes the text never holds.
    constexpr char kNames[] = "xyyyyyyyyyyyyyyyy";
    cursiv::LexRelay relay;
    relay.closes = true;
    relay.beforeOff = 0;
    relay.beforeLen = 1;
    relay.afterOff = 1;
    relay.afterLen = 16;
    cursiv::ByteSet pad;
    pad.add(' ');
    const cursiv::TextOffset run = 0xFFFFFFFFu - 8u;
    const auto len = static_cast<cursiv::TextOffset>(kText.size());
    CHECK(cursiv::lex_echo_padded(relay, pad, kNames, end - len, len, 0, 0,
                                  run) == 0);
  }
  CHECK(munmap(region, 2 * page) == 0);
#endif
}

// A `before` that the capture repeats stands at every byte of a long body:
// the places the echo stands are read along the text once, not once each.
void a_padded_closer_inside_a_repeated_capture_is_read_once() {
  constexpr cursiv::TextOffset n = 1000000;
  const std::string text = "<" + std::string(3 * n, 'a') + "b";
  constexpr char kNames[] = "ab";
  cursiv::LexRelay relay;
  relay.closes = true;
  relay.beforeLen = 1;
  relay.afterOff = 1;
  relay.afterLen = 1;
  cursiv::ByteSet pad;
  pad.add(' ');
  const auto len = static_cast<cursiv::TextOffset>(text.size());
  CHECK(cursiv::lex_echo_padded(relay, pad, kNames, text.data(), len, n + 1, 1,
                                n) == len);
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  lua_long_brackets();
  lua_lexes_the_same_from_every_form();
  cpp_raw_strings();
  twin_echo_outside_through();
  shared_openers();
  an_indented_closer_ends_a_heredoc();
  long_closer_search();
  an_echo_under_another_tag_is_refused();
  a_closer_run_stops_where_the_echo_starts();
  a_recursive_terminal_accepts_composed_delimiters();
  nested_delimiter_choices_are_flattened_for_the_native_search();
  a_continuation_byte_delimiter_is_not_searched_natively();
  a_nested_run_ends_at_depth_zero();
  a_nested_run_counts_every_opener_it_lists();
  delimiters_are_as_long_as_written();
  a_refusal_before_a_relay_leaves_its_first_byte_to_the_search();
  a_relayed_run_reads_every_rest_once();
  a_nested_search_remembers_what_it_settled();
  a_listed_search_reads_as_the_model();
  a_mark_run_is_read_once();
  a_closer_room_near_the_bound_does_not_wrap();
  a_padded_closer_inside_a_repeated_capture_is_read_once();
  return cursiv::test::report("LexRelay_test");
}
