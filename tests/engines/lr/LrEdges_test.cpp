#include <cstdint>
#include <cstdio>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/lr_rescan.hpp"
#include "cursiv/engines/shared/runtime/CancellationPoll.hpp"
#include "cursiv/test/Check.hpp"
#include "edges/generated/G/Module.hpp"
#include "edges/generated/G/Parser.hpp"
#include "edges/generated/G/Table.hpp"
#include "engines/lr/EdgesGrammar.hpp"
#include "engines/lr/RegionSkipGrammar.hpp"
#include "engines/lr/SectionTailGrammar.hpp"
#include "engines/lr/WideOptionalGrammar.hpp"
#include "sectiontailll/generated/Ll/Module.hpp"
#include "sectiontailll/generated/Ll/Table.hpp"
#include "sectiontaillr/generated/Lr/Module.hpp"
#include "sectiontaillr/generated/Lr/Parser.hpp"
#include "sectiontaillr/generated/Lr/Table.hpp"
#include "skiplr/generated/Lr/Module.hpp"
#include "wideoptionallr/generated/Lr/Module.hpp"

// Where a reading commits and where a token ends: committed repetitions,
// region edges and guarded alternatives.

namespace commits {

int run() {
  const auto p = tedges::generated::G::create_parser();
  const auto clean = [&](std::string_view body) {
    const std::string text = "commit " + std::string(body);
    const auto r = p->parse(text);
    return r.accepted && r.diagnostics.empty();
  };
  // Where the prefix can be read the item is owed, whether its body follows
  // or not.
  CHECK(!clean("sa"));
  CHECK(clean("saba"));
  CHECK(!clean("qab"));
  CHECK(clean("qabab"));
  // A prefix with an optional part still has to read something.
  CHECK(!clean("ea"));
  CHECK(!clean("exa"));
  CHECK(clean("eaba"));
  CHECK(clean("exaba"));
  // ...in a region that reads no blank as well.
  CHECK(clean("[aba]"));
  CHECK(!clean("[a]"));
  CHECK(!clean("[a ba]"));
  // An item that opens on several tokens is owed on any of them: nested
  // blocks fork nowhere.
  std::string nest = "commit k";
  for (int i = 0; i < 40; ++i) nest += " l = r t j = r";
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<tedges::generated::G::kTable>.parse(
      nest, cst, &stats, tedges::generated::G::kDrivePtr));
  CHECK(cst.diagnostics().empty());
  CHECK(stats.splits == 0);
  for (const auto text : {"k j = r t l = r t j = r l = r", "k j = r l = r"})
    CHECK(clean(text));
  std::string source = "commit h";
  for (int i = 0; i < 64; ++i) source += " , a";
  source += " b";
  const auto repeated = p->parse(source);
  CHECK(repeated.accepted && repeated.diagnostics.empty());
  const auto* doc = repeated.value_as<tcommit::Doc>();
  CHECK(doc != nullptr && doc->words.size() == 64);
  if (doc != nullptr)
    for (const auto word : doc->words) CHECK(word == "a");
  return 0;
}

}  // namespace commits

// The boundary before a region is the caller's wherever the region stands,
// and a refusal in a rule holds inside a region as outside.
namespace region_edges {

namespace {

// The regions' case, which its text follows with no gap.
constexpr std::string_view kHead = "region:";

bool clean(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse(std::string(kHead) + std::string(text));
  return r.accepted && r.diagnostics.empty() && r.value != nullptr;
}
bool repaired(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse(std::string(kHead) + std::string(text));
  return r.accepted && !r.diagnostics.empty();
}

void a_region_opening_with_a_rule_keeps_the_callers_boundary() {
  const std::unique_ptr<cursiv::Parser> p =
      tedges::generated::G::create_parser();
  CHECK(clean(*p, "x]"));
  CHECK(clean(*p, " x]"));
  CHECK(clean(*p, "x] x]"));
  CHECK(repaired(*p, "x ]"));
}

void a_region_past_the_head_keeps_the_callers_boundary() {
  const std::unique_ptr<cursiv::Parser> p =
      tedges::generated::G::create_parser();
  CHECK(clean(*p, "d ef x"));
  CHECK(repaired(*p, "d e f x"));
  // The blank the repair dropped was the keyword's reading's; the name read
  // in the tight region was refused on it first, and closes nothing past it.
  const std::string text = std::string(kHead) + "d ";
  const cursiv::ParseResult r = p->parse(text);
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  std::vector<cursiv::NextFeature> out;
  p->nextFeaturesAt(r.cst(), at, at, out);
  bool closes = false, opens = false;
  for (const auto& f : out) {
    closes = closes || p->getGrammar().terminalText(f.feature) == "]";
    opens = opens || p->getGrammar().terminalText(f.feature) == "e";
  }
  CHECK(!closes && opens);
}

void an_optional_head_keeps_the_callers_boundary() {
  const std::unique_ptr<cursiv::Parser> p =
      tedges::generated::G::create_parser();
  CHECK(clean(*p, "o b x"));
  CHECK(clean(*p, "o ab x"));
  CHECK(repaired(*p, "o a b x"));
}

void a_refusal_holds_inside_a_region() {
  const std::unique_ptr<cursiv::Parser> p =
      tedges::generated::G::create_parser();
  CHECK(clean(*p, "(foo)"));
  CHECK(repaired(*p, "(state)"));
  CHECK(clean(*p, "<foo>"));
  CHECK(repaired(*p, "<state>"));
}

void a_refusal_at_either_region_edge_keeps_its_boundaries() {
  const auto p = tedges::generated::G::create_parser();
  CHECK(clean(*p, "p ab c"));
  CHECK(clean(*p, "p abc"));
  CHECK(repaired(*p, "p a b c"));
  CHECK(repaired(*p, "p abz c"));
  CHECK(repaired(*p, "p zab c"));
}

void a_region_closing_on_a_rule_hands_back_the_callers_boundary() {
  const auto p = tedges::generated::G::create_parser();
  CHECK(clean(*p, "@ [*+- x"));
  CHECK(clean(*p, "@ [*+-\nx"));
  CHECK(clean(*p, "@ [*+-x"));
  CHECK(repaired(*p, "@ [* +- x"));
  CHECK(repaired(*p, "@ [*+ - x"));
  CHECK(clean(*p, "# +- x"));
  CHECK(repaired(*p, "# + - x"));
}

}  // namespace

int run() {
  a_region_opening_with_a_rule_keeps_the_callers_boundary();
  a_region_past_the_head_keeps_the_callers_boundary();
  an_optional_head_keeps_the_callers_boundary();
  a_refusal_holds_inside_a_region();
  a_refusal_at_either_region_edge_keeps_its_boundaries();
  a_region_closing_on_a_rule_hands_back_the_callers_boundary();
  return 0;
}

}  // namespace region_edges

// A guard closes the literal that spells it, not the terminal around it --
// in every engine, which read one grammar here.
namespace guarded_alt {

namespace {

bool clean(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse("guard " + std::string(text));
  return r.accepted && r.diagnostics.empty() && r.value != nullptr;
}
bool faulted(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse("guard " + std::string(text));
  return !r.diagnostics.empty();
}

void the_keyword_alternative_keeps_its_boundary(const cursiv::Parser& p) {
  CHECK(clean(p, "A and B"));
  CHECK(faulted(p, "A andB"));
}

void the_sign_alternative_has_none(const cursiv::Parser& p) {
  CHECK(clean(p, "A&&B"));
  CHECK(clean(p, "A &&B"));
  CHECK(clean(p, "A&& B"));
}

void a_guard_inside_a_body_holds_where_it_is_spelled(const cursiv::Parser& p) {
  CHECK(clean(p, "@end"));
  CHECK(faulted(p, "@end1"));
  CHECK(faulted(p, "@endx"));
}

// The run the guard closes is crossed blind by the walk, so its acceptance is
// read at the end alone. The boundary refuses there and admits inside: the
// match is the last position it admitted, never nothing.
void a_guard_on_a_run_is_read_at_every_position(const cursiv::Parser& p) {
  CHECK(clean(p, "aab"));
  CHECK(clean(p, "aaaaaaaaab"));
  CHECK(faulted(p, "ab"));
}

void every_engine(const cursiv::Parser& p) {
  the_keyword_alternative_keeps_its_boundary(p);
  the_sign_alternative_has_none(p);
  a_guard_inside_a_body_holds_where_it_is_spelled(p);
  a_guard_on_a_run_is_read_at_every_position(p);
}

}  // namespace

int run() {
  every_engine(*tedges::generated::G::create_parser());
  return 0;
}

}  // namespace guarded_alt

namespace case_regionskip {
// Regions naming trivia of their own, read by both engines: the region's
// configuration holds between its elements and in every rule it calls, its
// edges are the caller's, and what it keeps is a leaf of the tree.

namespace {

bool clean(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse(text);
  const bool ok = r.accepted && r.diagnostics.empty() && r.value != nullptr;
  if (!ok)
    std::printf("  not clean: \"%.*s\"\n", static_cast<int>(text.size()),
                text.data());
  return ok;
}
// Read with a repair, the first one at `at`: where the text breaks the law
// the case is about.
bool repaired(const cursiv::Parser& p, std::string_view text,
              cursiv::TextOffset at) {
  const cursiv::ParseResult r = p.parse(text);
  const bool ok =
      r.accepted && !r.diagnostics.empty() && r.diagnostics.front().begin == at;
  if (!ok)
    std::printf("  not repaired at %u: \"%.*s\"\n", at,
                static_cast<int>(text.size()), text.data());
  return ok;
}
// ...and every repair within [from, to]: the text around is read clean.
bool local(const cursiv::Parser& p, std::string_view text,
           cursiv::TextOffset from, cursiv::TextOffset to) {
  const cursiv::ParseResult r = p.parse(text);
  bool ok = r.accepted && r.value != nullptr && !r.diagnostics.empty();
  for (const auto& d : r.diagnostics) ok = ok && d.begin >= from && d.end <= to;
  if (!ok)
    std::printf("  not local: \"%.*s\"\n", static_cast<int>(text.size()),
                text.data());
  return ok;
}

// The leaves in document order, a kept trivia in brackets.
std::string leaves(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse(text);
  std::string out;
  r.cst().visit_leaves([&](const cursiv::CstLeaf& l) {
    if (l.begin == l.end) return;
    if (!out.empty()) out += ' ';
    const std::string_view s = text.substr(l.begin, l.end - l.begin);
    out += l.hidden ? "[" + std::string(s) + "]" : std::string(s);
  });
  return out;
}

// The literals a caret at the end of `text` is offered.
std::string offered(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse(text);
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), at, at, out);
  std::string seen;
  for (const auto& f : out) {
    const std::string_view t = p.getGrammar().terminalText(f.feature);
    if (!t.empty() && seen.find(t) == std::string::npos) seen += t;
  }
  return seen;
}

// A region that skips nothing: its edges are the caller's, and a rule it calls
// reads tight too.
void a_tight_region(const cursiv::Parser& p) {
  CHECK(clean(p, "use a.b.c;"));
  CHECK(clean(p, "use  a.b;"));
  CHECK(repaired(p, "use a . b;", 5));
  CHECK(repaired(p, "use a. b;", 6));
  CHECK(repaired(p, "use a.b ;", 7));
  CHECK(repaired(p, "use a.#c b;", 6));
  CHECK(clean(p, "neg  --5"));
  CHECK(clean(p, "neg 5"));
  CHECK(repaired(p, "neg - -5", 5));
  CHECK(repaired(p, "neg -- 5", 6));
  CHECK(clean(p, "pick  +a"));
  CHECK(repaired(p, "pick + a", 6));
  CHECK(clean(p, "via  .a;"));
  CHECK(repaired(p, "via . a;", 5));
}

// A region inside a tight one skips blanks again, and only blanks.
void an_inner_region_reads_its_own_trivia(const cursiv::Parser& p) {
  CHECK(clean(p, "do f( x  y )end"));
  CHECK(clean(p, "do f(x) end"));
  CHECK(repaired(p, "do f ( x ) end", 4));
  CHECK(repaired(p, "do f(x #c) end", 7));
  CHECK(leaves(p, "dd f( x #c y )end") == "dd f ( x [#c] y ) end");
  CHECK(repaired(p, "dd f ( x ) end", 4));
}

void what_a_region_hides_is_a_leaf(const cursiv::Parser& p) {
  CHECK(clean(p, "hb a ! b end"));
  CHECK(leaves(p, "hb a ! b end") == "hb a [!] b end");
  CHECK(leaves(p, "hb a b ! end") == "hb a b [!] end");
  CHECK(repaired(p, "hb a #c b end", 5));
  CHECK(repaired(p, "hb a b end !", 11));
}

// The outer region drops comments, the inner one does not read them, and past
// both the grammar keeps them: a region's trailing trivia is its caller's.
void nested_regions_hand_trivia_back(const cursiv::Parser& p) {
  CHECK(clean(p, "m a: ? end"));
  CHECK(clean(p, "m a : ? end"));
  CHECK(repaired(p, "m a #c : ? end", 4));
  CHECK(leaves(p, "m a: #c ? end") == "m a : ? end");
  CHECK(leaves(p, "m a: ? #c end") == "m a : ? [#c] end");
  CHECK(leaves(p, "m #c a: ? end") == "m [#c] a : ? end");
}

// A newline ends a line, and is a blank between parentheses: a rule read
// inside and outside them is read under each.
void a_newline_is_trivia_between_parentheses(const cursiv::Parser& p) {
  CHECK(clean(p, "let x = 1\n"));
  CHECK(clean(p, "let x = (1,\n 2)\n"));
  CHECK(clean(p, "let x = ((1, a),\n\n (b))\n"));
  CHECK(leaves(p, "let x = (1, #c\n2)\n") == "let x = ( 1 , [#c] 2 ) \n");
  CHECK(clean(p, "let x = (a\n= 1, b)\n"));
  CHECK(repaired(p, "let x = a\n= 1\n", 10));
  CHECK(repaired(p, "let x =\n 1\n", 7));
  CHECK(repaired(p, "let x = 1", 9));
  CHECK(clean(p, "use a.b; do f(x)end hb a b end m a: ? end let y = (2)\n"));
}

// The caret reads a region under the region's configuration, and a repair
// inside one is local to it.
void the_caret_and_the_repair_read_the_region(const cursiv::Parser& p) {
  const std::string path = offered(p, "use a");
  CHECK(path.find('.') != std::string::npos &&
        path.find(';') != std::string::npos);
  const std::string tuple = offered(p, "let x = (1,\n");
  CHECK(tuple.find('(') != std::string::npos);
  const std::string after = offered(p, "let x = (1\n");
  CHECK(after.find(',') != std::string::npos &&
        after.find(')') != std::string::npos);
  const std::string sign = offered(p, "neg ");
  CHECK(sign.find('-') != std::string::npos);
  const std::string either = offered(p, "pick ");
  CHECK(either.find('+') != std::string::npos &&
        either.find('*') != std::string::npos);
  CHECK(offered(p, "via ").find('.') != std::string::npos);
  CHECK(offered(p, "pick + ").empty());
  CHECK(repaired(p, "let x = (1,,2)\n", 11));
  CHECK(repaired(p, "use a.;", 6));
  CHECK(local(p, "use a.; let y = 2\n", 4, 7));
  CHECK(local(p, "let x = (1,,2) let y = 2\n", 8, 14));
}

// A blank the grammar skips is a token of a rule only a tight region reaches.
void a_region_reads_a_skipped_terminal(const cursiv::Parser& p) {
  CHECK(clean(p, "band [a b]"));
  CHECK(leaves(p, "band [a b]") == "band [ a   b ]");
  CHECK(repaired(p, "band [ab]", 8));
  CHECK(repaired(p, "band [a  b ]", 10));
}

void each(const char* name, const cursiv::Parser& p) {
  std::printf("%s\n", name);
  a_tight_region(p);
  an_inner_region_reads_its_own_trivia(p);
  what_a_region_hides_is_a_leaf(p);
  nested_regions_hand_trivia_back(p);
  a_newline_is_trivia_between_parentheses(p);
  the_caret_and_the_repair_read_the_region(p);
  a_region_reads_a_skipped_terminal(p);
}

// A condition at the tail of a region that skips newlines reads the gap in
// front of the caller's newline token.
void a_condition_at_a_region_tail(const cursiv::Parser& p) {
  std::printf("LR tail\n");
  CHECK(clean(p, "tail (a)\nlet x = 1\n"));
  CHECK(clean(p, "tail ( a\n) #c\n"));
}

}  // namespace

int run() {
  const auto lr = tskip::generated::Lr::create_parser();
  each("LR", *lr);
  a_condition_at_a_region_tail(*lr);
  return 0;
}
}  // namespace case_regionskip

namespace case_wideoptional {
// Bodies of twenty optional positions that sit on a `skip()` region's edge or
// assert the end of the text: fixed expectations check the words that are
// present and their order in the tree.

namespace {

struct Random {
  std::uint32_t state = 12345;
  unsigned next(unsigned below) {
    state = state * 1664525u + 1013904223u;
    return (state >> 8) % below;
  }
};

bool clean(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty() && r.value != nullptr;
}

std::string join(const std::vector<std::string>& words) {
  std::string out;
  for (const std::string& w : words) out += (out.empty() ? "" : ",") + w;
  return out;
}

// What the tree holds: the words, the extra words, the marks.
std::string shape(const cursiv::ParseResult& r) {
  std::vector<std::string> words, extra;
  std::size_t marks = 0;
  if (const auto* doc = r.value_as<twide::ast::Doc>()) {
    for (const std::string_view w : doc->words) words.emplace_back(w);
    for (const std::string_view w : doc->extra) extra.emplace_back(w);
    marks = doc->marks.size();
  }
  return join(words) + "/" + join(extra) + "/" + std::to_string(marks);
}

struct Engine {
  const cursiv::Parser& lr;

  void read(const std::string& text, const std::string& expected) const {
    const cursiv::ParseResult r = lr.parse(text);
    const bool ok = clean(r) && shape(r) == expected;
    if (!ok)
      std::printf("  LR read \"%s\" as %s\n", text.c_str(), shape(r).c_str());
    CHECK(ok);
  }

  void read(const std::string& text,
            const std::vector<std::string>& words) const {
    read(text, join(words) + "//0");
  }

  void refuse(const std::string& text) const { CHECK(!clean(lr.parse(text))); }
};

std::string blanks(Random& rng) { return std::string(rng.next(3), ' '); }

// The positions that are present, in the order the grammar spells them, and a
// text with the blanks the caller may put between regions.
void positions(const Engine& e, std::string_view head,
               const std::vector<std::string>& all, std::string_view tail) {
  Random rng;
  const auto text = [&](const std::vector<bool>& present, bool blank) {
    std::string out(head);
    out += ' ';
    for (std::size_t i = 0; i < all.size(); ++i)
      if (present[i]) out += (blank ? blanks(rng) : "") + all[i];
    return out + std::string(tail);
  };
  const auto expect = [&](const std::vector<bool>& present) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < all.size(); ++i)
      if (present[i]) out.push_back(all[i]);
    return out;
  };
  std::vector<bool> present(all.size(), false);
  e.read(text(present, false), expect(present));
  present.assign(all.size(), true);
  e.read(text(present, false), expect(present));
  e.read(text(present, true), expect(present));
  for (std::size_t i = 0; i < all.size(); ++i) {
    present.assign(all.size(), false);
    present[i] = true;
    e.read(text(present, true), expect(present));
    present.assign(all.size(), true);
    present[i] = false;
    e.read(text(present, true), expect(present));
  }
  for (unsigned round = 0; round < 300; ++round) {
    for (std::size_t i = 0; i < all.size(); ++i) present[i] = rng.next(2) != 0;
    e.read(text(present, true), expect(present));
  }
}

void tight_regions(const Engine& e) {
  std::vector<std::string> all;
  for (const char c : {'p', 'q'})
    for (int i = 0; i < 10; ++i) all.push_back(c + std::to_string(i));
  positions(e, "e", all, " end");
  e.refuse("e p1 p0 end");
  e.refuse("e q0 p9 end");
  e.refuse("e p0 p0 end");
  e.refuse("e p0 p1");
  e.refuse("e p0 end end");
}

void a_region_opening_on_optional_parts(const Engine& e) {
  const std::string letters = "abcdefghijklmnopqr";
  Random rng;
  const auto read = [&](const std::string& present) {
    std::string inside, kept;
    for (const char c : letters)
      if (present.find(c) != std::string::npos) inside += c;
    std::vector<std::string> expected;
    for (const char c : inside) expected.emplace_back(1, c);
    expected.emplace_back("z");
    e.read("h" + std::string(1 + rng.next(3), ' ') + inside + "z" +
               blanks(rng) + " end",
           expected);
  };
  read("");
  read(letters);
  for (const char c : letters) {
    read(std::string(1, c));
    std::string rest = letters;
    rest.erase(rest.find(c), 1);
    read(rest);
  }
  for (unsigned round = 0; round < 300; ++round) {
    std::string present;
    for (const char c : letters)
      if (rng.next(2) != 0) present += c;
    read(present);
  }
  e.refuse("h a bz end");
  e.refuse("h ab z end");
  e.refuse("h abz  end z");
  e.refuse("h baz end");
  e.refuse("h aaz end");
  e.refuse("h ab end");
}

void optional_regions_of_two_tokens(const Engine& e) {
  std::vector<std::string> all;
  for (int i = 0; i < 10; ++i) {
    all.push_back("m" + std::to_string(i));
    all.push_back("n" + std::to_string(i));
  }
  Random rng;
  const auto read = [&](unsigned mask, bool blank) {
    std::string text = "w ";
    std::vector<std::string> expected;
    for (int i = 0; i < 10; ++i) {
      if (((mask >> i) & 1u) == 0) continue;
      text += (blank ? blanks(rng) : " ") + all[2 * i] + all[2 * i + 1];
      expected.push_back(all[2 * i]);
      expected.push_back(all[2 * i + 1]);
    }
    e.read(text + " end", expected);
  };
  read(0, false);
  read(0x3ff, false);
  read(0x3ff, true);
  for (int i = 0; i < 10; ++i) {
    read(1u << i, true);
    read(0x3ffu & ~(1u << i), true);
  }
  for (unsigned round = 0; round < 300; ++round) read(rng.next(1u << 10), true);
  e.refuse("w m0 n0 end");
  e.refuse("w m0n0 m1 n1 end");
  e.refuse("w n0m0 end");
  e.refuse("w m1n1m0n0 end");
  e.refuse("w m0 end");
}

void end_assertions(const Engine& e) {
  std::vector<std::string> run;
  std::string text = "d";
  e.read(text, run);
  for (int i = 0; i < 10; ++i) {
    run.push_back("s" + std::to_string(i));
    text += " s" + std::to_string(i);
    e.read(text, run);
    e.read(text + "   ", run);
  }
  e.refuse("d s1");
  e.refuse("d s0 s2");
  e.refuse("d s0 s1 s1");
  e.refuse("d s9");
  e.refuse("d s0 s1 s2 s3 s4 s5 s6 s7 s8 s9 s9");
}

// Two optional regions on the same token, which of them keeps it.
void overlapping_regions(const Engine& e) {
  e.read("o end", "//0");
  e.read("o x end", "x//0");
  e.read("o y end", "/y/0");
  e.read("o x x end", "x/x/0");
  e.read("o x y end", "x/y/0");
  e.read("o x x x end", "x,x/x/0");

}

// The end assertion ahead of optional parts and a rule that builds a node
// from nothing.
void an_assertion_before_a_node_built_from_nothing(const Engine& e) {
  e.read("m", "//1");
  e.read("m t", "t//1");
  e.read("m t k", "t//1");
  e.read("m t c0", "t,c0//1");
  e.read("m t c0 c8 k", "t,c0,c8//1");
  e.refuse("m t c1 c0");
  e.refuse("m t c3 c3");
  e.refuse("m k");
  e.refuse("m c0");
}

// The literals a caret at the end of `text` is offered.
std::set<std::string> offered(const cursiv::Parser& p,
                              const std::string& text) {
  const cursiv::ParseResult r = p.parse(text);
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), at, at, out);
  std::set<std::string> seen;
  for (const auto& f : out) {
    const std::string_view t = p.getGrammar().terminalText(f.feature);
    if (!t.empty()) seen.emplace(t);
  }
  return seen;
}

std::string listed(const std::set<std::string>& offers) {
  std::string out;
  for (const std::string& t : offers) out += " " + t;
  return out;
}

void offers(const Engine& e, const std::string& text,
            const std::set<std::string>& expected) {
  const auto actual = offered(e.lr, text);
  if (actual != expected)
    std::printf("  LR offers at \"%s\":%s\n", text.c_str(), listed(actual).c_str());
  CHECK(actual == expected);
}

// The positions still open are the literals the parser offers at the caret.
void the_caret_is_offered_the_positions_still_open(const Engine& e) {
  std::vector<std::string> edges, letters, pairs;
  for (const char c : {'p', 'q'})
    for (int i = 0; i < 10; ++i) edges.push_back(c + std::to_string(i));
  for (const char c : std::string("abcdefghijklmnopqr"))
    letters.emplace_back(1, c);
  for (int i = 0; i < 10; ++i) pairs.push_back("m" + std::to_string(i));
  const auto after = [](const std::vector<std::string>& all, std::size_t k,
                        std::string_view last) {
    std::set<std::string> open(all.begin() + k, all.end());
    open.emplace(last);
    return open;
  };
  for (const std::size_t k : {0, 3, 10, 19}) {
    std::string text = "e ";
    for (std::size_t i = 0; i < k; ++i) text += edges[i] + " ";
    offers(e, text, after(edges, k, "end"));
  }
  for (const std::size_t k : {0, 3, 17}) {
    const std::string text =
        "h " +
        std::accumulate(letters.begin(), letters.begin() + k, std::string());
    offers(e, text, after(letters, k, "z"));
  }
  for (const std::size_t k : {0, 4, 9}) {
    std::string text = "w ";
    for (std::size_t i = 0; i < k; ++i)
      text += pairs[i] + "n" + std::to_string(i) + " ";
    offers(e, text, after(pairs, k, "end"));
  }
}

// The gap in front of the first token of the document is the grammar's, the
// token a region's.
void a_region_at_the_head_of_the_document(const Engine& e) {
  e.read("  ab;", "a,b//0");
  e.read("b;", "b//0");
  e.refuse("a b;");
  for (const char* text : {"", " ", "  "}) offers(e, text, {"a", "b", "d", "e", "h", "m", "o", "r", "w"});
  offers(e, "a", {"b"});
  offers(e, " a", {"b"});
  offers(e, "a ", {});
  offers(e, "  b", {";"});
}

void a_required_token_after_an_assertion(const Engine& e) {
  e.read("r u b", "u,b//0");
  e.read("r u b c0 c5 c7", "u,b,c0,c5,c7//0");
  e.read("r u b c0 c1 c2 c3 c4 c5 c6 c7", "u,b,c0,c1,c2,c3,c4,c5,c6,c7//0");
  e.refuse("r");
  e.refuse("r b");
  e.refuse("r u");
  e.refuse("r u c0");
  e.refuse("r u b c1 c0");
}

}  // namespace

int run() {
  const auto lr = twide::generated::Lr::create_parser();
  const Engine e{*lr};
  tight_regions(e);
  a_region_opening_on_optional_parts(e);
  optional_regions_of_two_tokens(e);
  end_assertions(e);
  overlapping_regions(e);
  a_required_token_after_an_assertion(e);
  an_assertion_before_a_node_built_from_nothing(e);
  the_caret_is_offered_the_positions_still_open(e);
  a_region_at_the_head_of_the_document(e);
  return 0;
}
}  // namespace case_wideoptional

namespace case_sectiontail {
// A run that no token of an embedded grammar closes is looked through once:
// the bytes the recovery walks grow with the run, not with its square, in both
// engines and with the repair budget spent, and a cancelled parse is told
// within a run of polls.

namespace {

std::uint64_t g_steps = 0;

template <const auto& Table>
struct Counted : std::remove_cvref_t<decltype(Table)> {
  using Base = std::remove_cvref_t<decltype(Table)>;
  constexpr Counted() : Base(Table) {}
  std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    ++g_steps;
    return Base::next_byte(s, b);
  }
};

inline constexpr Counted<tsection::generated::Lr::kTable> kLr{};
inline constexpr Counted<tsection::generated::Ll::kTable> kLl{};

constexpr unsigned kRuns[] = {4096, 16384, 65536};

std::string open_run(unsigned n) { return "<" + std::string(n, 'a') + ">"; }

template <const auto& A>
std::uint32_t section_state() {
  for (std::uint32_t s = 0; s < A.mode.size(); ++s)
    if (cursiv::lr_text_close<A>(s) != cursiv::kEofSymbol) return s;
  return 0;
}

struct LrRoom {
  explicit LrRoom(const std::string& text)
      : cst(text), builder(cst), context(text, builder) {}
  cursiv::Cst cst;
  cursiv::CstBuilder builder;
  cursiv::LrContext context;
};

template <const auto& T>
std::uint8_t ll_section_of_inner() {
  for (std::uint8_t k = 0; k < T.nsections; ++k)
    if (T.sectionEnd[k] != cursiv::kEofSymbol) return k + 1;
  return 0;
}

struct LlRoom {
  explicit LlRoom(const std::string& text)
      : cst(text), builder(cst), context(text, builder) {
    builder.reserve_stream(text.size());
    builder.bind_stream();
    context.section = ll_section_of_inner<kLl>();
  }
  cursiv::Cst cst;
  cursiv::CstBuilder builder;
  cursiv::LlContext context;
};

void the_section_end_is_looked_for_in_linear_work() {
  for (const unsigned n : kRuns) {
    const std::string text = open_run(n);
    {
      LrRoom room(text);
      std::uint64_t read = 0;
      g_steps = 0;
      const auto end = cursiv::lr_text_end<kLr>(
          room.context, section_state<kLr>(), 1, room.context.len, read);
      CHECK(end == n + 1);
      CHECK(g_steps < 40ull * n);
      CHECK(read >= n && read < 40ull * n);
    }
    {
      LlRoom room(text);
      g_steps = 0;
      const auto end = cursiv::ll_text_end<kLl>(room.context, 1,
                                                room.context.len);
      CHECK(end == n + 1);
      CHECK(g_steps < 40ull * n);
    }
  }
}

void a_whole_parse_of_the_run_is_linear(const cursiv::RepairBudget& budget) {
  std::uint64_t steps[std::size(kRuns)] = {};
  for (std::size_t k = 0; k < std::size(kRuns); ++k) {
    const std::string text = open_run(kRuns[k]);
    cursiv::LrDrive drive = *tsection::generated::Lr::kDrivePtr;
    drive.walk = [](const char* src, cursiv::TextOffset len,
                    std::uint16_t mode, cursiv::TextOffset at) {
      return cursiv::lr_match_text<kLr>(src, len, mode, at);
    };
    cursiv::Cst cst;
    g_steps = 0;
    CHECK(cursiv::lr_parser<kLr>.parse(text, cst, nullptr, &drive, budget));
    CHECK(!cst.diagnostics().empty());
    steps[k] = g_steps;
    CHECK(g_steps < 60ull * kRuns[k]);
  }
  CHECK(steps[2] < 24 * steps[0]);
}

void the_engines_tell_a_cancelled_scan() {
  cursiv::CancellationTokenSource source;
  source.request_stop();
  const cursiv::CancellationToken token = source.get_token();
  const std::string text = open_run(kRuns[2]);
  {
    LrRoom room(text);
    room.context.stop = cursiv::CancellationPoll(token);
    std::uint64_t read = 0;
    bool told = false;
    try {
      (void)cursiv::lr_text_end<kLr>(room.context, section_state<kLr>(), 1,
                                     room.context.len, read);
    } catch (const cursiv::OperationCancelled&) {
      told = true;
    }
    CHECK(told);
  }
  {
    LlRoom room(text);
    room.context.stop = cursiv::CancellationPoll(token);
    bool told = false;
    try {
      (void)cursiv::ll_text_end<kLl>(room.context, 1, room.context.len);
    } catch (const cursiv::OperationCancelled&) {
      told = true;
    }
    CHECK(told);
  }
}

void both_engines_read_the_run_as_one_fault() {
  const auto ll = tsection::generated::Ll::create_parser();
  const auto lr = tsection::generated::Lr::create_parser();
  for (const unsigned n : {16u, 4096u}) {
    const std::string text = open_run(n);
    const auto a = ll->parse(text);
    const auto b = lr->parse(text);
    CHECK(a.accepted && b.accepted);
    CHECK(!a.diagnostics.empty() && !b.diagnostics.empty());
  }
}

}  // namespace

int run() {
  the_section_end_is_looked_for_in_linear_work();
  a_whole_parse_of_the_run_is_linear({});
  a_whole_parse_of_the_run_is_linear({64, 0, 64, 64});
  the_engines_tell_a_cancelled_scan();
  both_engines_read_the_run_as_one_fault();
  return 0;
}
}  // namespace case_sectiontail

int main() {
  int movedStatus = 0;
  movedStatus |= case_regionskip::run();
  movedStatus |= case_wideoptional::run();
  movedStatus |= case_sectiontail::run();
  int status = 0;
  status |= commits::run();
  status |= region_edges::run();
  status |= guarded_alt::run();
  return (cursiv::test::report("LrEdges_test") | status) | movedStatus;
}
