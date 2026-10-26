#include "characterboundsll/generated/BoundsLl/Module.hpp"
#include "characterboundslr/generated/BoundsLr/Module.hpp"
#include "charactersll/generated/Ll/Module.hpp"
#include "characterslr/generated/Lr/Module.hpp"
#include "characterslr/generated/Lr/Table.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/CharactersGrammar.hpp"
#include <cstddef>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace characters_cases {

// A class reads one character -- a well-formed scalar whole, or the byte where
// none starts -- in both engines.

namespace {

constexpr std::string_view kAcute = "\xC3\xA9";
constexpr std::string_view kLone = "\xE9";

const char* engine = "";

bool accepted(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty();
}

std::string read(const cursiv::Parser& parser, std::string_view text) {
  const std::string source(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const doc = r.value_as<tchars::ast::Doc>();
  if (!accepted(r) || doc == nullptr) return "<refused>";
  std::string out;
  for (const tchars::ast::Item* const item : doc->items) {
    if (!out.empty()) out += '|';
    out += item->text;
  }
  return out;
}

void check(const cursiv::Parser& parser, std::string_view text,
           std::string_view expected) {
  const std::string got = read(parser, text);
  if (got != expected)
    std::printf("  [%s] %.*s\n    got      %s\n    expected %.*s\n", engine,
                static_cast<int>(text.size()), text.data(), got.c_str(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
}

std::string with(std::string_view tag, std::string_view body) {
  return std::string(tag) + std::string(body);
}

void whole_scalars(const cursiv::Parser& p) {
  for (const std::string_view scalar :
       {std::string_view("a"), kAcute, std::string_view("\xE2\x82\xAC"),
        std::string_view("\xF0\x9D\x92\xB3")})
    check(p, with("one:", scalar), scalar);
  check(p, with("two:", kAcute), "<refused>");
  check(p, "two:ab", "ab");
  check(p, with("two:", std::string(kAcute) + "a"), std::string(kAcute) + "a");
}

void quotes(const cursiv::Parser& p) {
  const std::string acute = "'" + std::string(kAcute) + "'";
  const std::string combining = "'e\xCC\x81'";
  for (const char* tag : {"qa:", "qn:"}) {
    check(p, with(tag, acute), acute);
    check(p, with(tag, combining), "<refused>");
    check(p, with(tag, "'a'"), "'a'");
  }
  check(p, with("qn:", "'''"), "<refused>");
}

void a_stray_byte_stands_alone(const cursiv::Parser& p) {
  for (const char* tag : {"sn:", "sa:"}) {
    for (const std::string_view stray :
         {std::string_view("\xFF"), std::string_view("\xC3"),
          std::string_view("\xE2\x82"), std::string_view("\xE2"),
          std::string_view("\xF0\x9F\x98"), std::string_view("\x80"),
          std::string_view("\xED\xA0\x80"), std::string_view("\xC0\x80")}) {
      const std::string text = "\"a" + std::string(stray) + "\"";
      check(p, with(tag, text), text);
    }
    const std::string closed = "\"" + std::string(kAcute) + "\"";
    check(p, with(tag, closed), closed);
  }
  check(p, "paren:\xC3(", "\xC3(");
  check(p, "paren:\xC3\xA9(", std::string(kAcute) + "(");
  check(p, "chars:\xC3(", "\xC3|(");
  check(p, "chars:\xE2\x82\"", "\xE2|\x82|\"");
  check(p, "chars:\xE2\x82\xAC\"", "\xE2\x82\xAC|\"");
  check(p, "chars:\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80");
  check(p, "chars:\xF0\x9F\x98", "\xF0|\x9F|\x98");
  check(p, "chars:\xED\xA0\x80", "\xED|\xA0|\x80");
  check(p, "chars:\xC0\x80", "\xC0|\x80");
  check(p, "chars:\xF4\x90\x80\x80", "\xF4|\x90|\x80|\x80");
}

void a_byte_is_one_byte(const cursiv::Parser& p) {
  check(p, with("bytes:", kAcute), "\xC3|\xA9");
  check(p, "bytes:a\xFF", "a|\xFF");
  check(p,
        "rb:a\xC3"
        "b",
        "");
  check(p,
        "rb:a\xFF"
        "b",
        "");
  check(p, "rb:ab", "<refused>");
  check(p, "rc1:x", "");
  check(p, with("rc2:", kAcute), "<refused>");
  check(p, with("rc3:", kAcute), "");
  check(p, "rc4:}", "");
  check(p, with("rc5:", kLone), "");
  check(p, with("rc6:", kLone), "");
  check(p, "rt1:x", "x");
  check(p, with("rt1:", kAcute), "<refused>");
  check(p, with("rt1:", kLone), kLone);
  check(p, with("rt3:", kAcute), kAcute);
  check(p, with("rt3:", kLone), kLone);
}

void classes(const cursiv::Parser& p) {
  check(p, "twice:aaa", "aaa");
  check(p, with("twice:", kAcute), "<refused>");
  check(p, with("twice:", kLone), "<refused>");
  const std::string mixed =
      std::string("a") + std::string(kAcute) + "\xFF" + " 9";
  check(p, with("whole:", mixed), mixed);
  check(p, with("acc:", std::string("a") + std::string(kAcute)),
        std::string("a") + std::string(kAcute));
  check(p, "acc:\xC3\x89", "<refused>");
  check(p, with("nw:", kAcute), kAcute);
  check(p, with("strict:", kAcute), kAcute);
  check(p, with("strict:", kLone), "<refused>");
  check(p, "strict:\xFF", "<refused>");
}

void the_scalar_and_the_byte(const cursiv::Parser& p) {
  const auto row = [&](std::string_view tag, bool onAcute, bool onLone) {
    const std::string acute = with(tag, kAcute), lone = with(tag, kLone);
    check(p, acute, onAcute ? std::string(kAcute) : "<refused>");
    check(p, lone, onLone ? std::string(kLone) : "<refused>");
  };
  row("cre:", true, false);
  row("crx:", true, false);
  row("ne:", false, true);
  row("one:", true, true);
  row("l:", true, false);
  row("nl:", false, true);
  row("a:", false, false);
  row("na:", true, true);
  row("nna:", false, false);
  row("either:", true, true);
  check(p, "either:a", "a");
  row("nd:", true, true);
  row("nnd:", false, false);
  check(p, "nl:1", "1");
  check(p, "l:a", "a");
  check(p, "nl:a", "<refused>");
}

void a_word_boundary_reads_a_character(const cursiv::Parser& p) {
  check(p, "kw:if", "if");
  check(p, with("kw:", std::string("if") + std::string(kAcute)), "<refused>");
  check(p, "kw:if_", "<refused>");
  check(p, "kw:if(", "<refused>");
  check(p, "kwt:if(", "if|(");
  check(p, "kwt:if\xFF", "if|\xFF");
  check(p, "kwt:if\xC3", "if|\xC3");
  check(p, "kwt:if\xC3(", "if|\xC3|(");
  check(p, "kwt:if\xE2\x82", "if|\xE2|\x82");
  check(p, with("kwt:", std::string("if") + std::string(kAcute)), "<refused>");
  check(p, with("kwt:", std::string("if") + "\xE6\x97\xA5"), "<refused>");
  check(p, with("kwt:", std::string("if") + "\xE2\x82\xAC"), "if|\xE2\x82\xAC");
}

// The characters of a text, by the rule: a well-formed scalar, else a byte.
std::size_t scalar_width(std::string_view s) {
  const auto b = [&](std::size_t k) {
    return k < s.size() ? static_cast<unsigned char>(s[k]) : 0u;
  };
  const auto cont = [&](std::size_t k, unsigned lo, unsigned hi) {
    return b(k) >= lo && b(k) <= hi;
  };
  const unsigned lead = b(0);
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) return cont(1, 0x80, 0xBF) ? 2 : 0;
  if (lead >= 0xE0 && lead <= 0xEF) {
    const unsigned lo = lead == 0xE0 ? 0xA0 : 0x80;
    const unsigned hi = lead == 0xED ? 0x9F : 0xBF;
    return cont(1, lo, hi) && cont(2, 0x80, 0xBF) ? 3 : 0;
  }
  if (lead >= 0xF0 && lead <= 0xF4) {
    const unsigned lo = lead == 0xF0 ? 0x90 : 0x80;
    const unsigned hi = lead == 0xF4 ? 0x8F : 0xBF;
    return cont(1, lo, hi) && cont(2, 0x80, 0xBF) && cont(3, 0x80, 0xBF) ? 4
                                                                         : 0;
  }
  return 0;
}

std::vector<std::string> characters(std::string_view s) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < s.size();) {
    const std::size_t n = scalar_width(s.substr(i));
    out.emplace_back(s.substr(i, n == 0 ? 1 : n));
    i += n == 0 ? 1 : n;
  }
  return out;
}

std::string joined(const std::vector<std::string>& parts) {
  std::string out;
  for (const std::string& part : parts) {
    if (!out.empty()) out += '|';
    out += part;
  }
  return out;
}

// A loop over every character but one ends before that character, whole: a
// run never stops between the bytes of a scalar.
void a_run_ends_at_a_character(const cursiv::Parser& p, std::string_view text) {
  const std::vector<std::string> chars = characters(text);
  for (const auto& [tag, stop] :
       {std::pair<const char*, std::string_view>{"rna:", "a"},
        {"rne:", kAcute}}) {
    std::size_t k = 0;
    while (k < chars.size() && chars[k] != stop) ++k;
    std::string want;
    for (std::size_t i = 0; i < chars.size(); ++i)
      want += (i < k ? "" : "|") + chars[i];
    check(p, with(tag, text), k == 0 ? "<refused>" : want);
  }
}

// Every text over the bytes that make and break scalars reads as the rule
// reads it, one character at a time, whatever surrounds it.
void any_text(const cursiv::Parser& p) {
  const unsigned char alphabet[] = {'a',  'b',  '"',  ' ',  0x80, 0x8F, 0x90,
                                    0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xA9, 0xC2,
                                    0xC3, 0xDF, 0xE0, 0xE1, 0xED, 0xEF, 0xF0,
                                    0xF1, 0xF4, 0xF5, 0xFF};
  std::mt19937 gen(20261021);
  for (unsigned round = 0; round < 1500; ++round) {
    std::string text;
    const unsigned size = 1 + gen() % 7;
    for (unsigned i = 0; i < size; ++i)
      text += static_cast<char>(alphabet[gen() % std::size(alphabet)]);
    const std::vector<std::string> chars = characters(text);
    a_run_ends_at_a_character(p, text);
    check(p, with("chars:", text), joined(chars));
    check(p, with("two:", text), chars.size() == 2 ? text : "<refused>");
    bool hasA = false, stray = false;
    for (const std::string& c : chars) {
      hasA = hasA || c == "a";
      stray =
          stray || (c.size() == 1 && static_cast<unsigned char>(c[0]) >= 0x80);
    }
    check(p, with("ncs:", text), hasA ? "<refused>" : joined(chars));
    check(p, with("strict:", text), stray ? "<refused>" : text);
    std::string quoted = "\"";
    for (const char c : text)
      if (c != '"') quoted += c;
    quoted += '"';
    check(p, with("sn:", quoted), quoted);
    check(p, with("sa:", quoted), quoted);
  }
}

void gaps(const cursiv::Parser& p) {
  const auto holds = [&](std::string_view text, bool expected) {
    const std::string source = "gaps:" + std::string(text);
    const cursiv::ParseResult r = p.parse(source);
    if (accepted(r) != expected)
      std::printf("  [%s] %.*s expected %s\n", engine,
                  static_cast<int>(text.size()), text.data(),
                  expected ? "accepted" : "refused");
    CHECK(accepted(r) == expected);
  };
  holds("hn a b", true);
  holds("hn ab", false);
  holds(
      "hn a\xC3\xA9"
      "b",
      false);
  holds(
      "hn a\xC3\xA9\xE9"
      "b",
      true);
  holds(
      "hn a\xA9"
      "b",
      true);
  holds("hn a\xC3\xA9 b", true);
  holds(
      "hn a\xE2\x82"
      "b",
      true);
  holds(
      "isn a\xE9"
      "b",
      true);
  holds(
      "isn a\xC3\xA9"
      "b",
      false);
  holds("isn a b", true);
  holds("isn a  b", false);
  holds("hbyte a b", true);
  holds("hbyte ab", false);
  holds(
      "isbb a\xC3\xA9"
      "b",
      true);
  holds("isbb a b", false);
  holds("isbb a  b", true);
  holds(
      "isaa a\xC3\xA9"
      "b",
      false);
  holds("isaa a  b", true);
  holds("isaa a\xC3\xA9 b", true);
  holds("isaa a\xE9 b", true);
  holds(
      "isaa a\xC3\xA9\xA9"
      "b",
      true);
  holds(
      "isaa a\xE2\x82\xAC"
      "b",
      false);
  holds(
      "isaa a\xE2\x82"
      "b",
      true);
}

// The complement of a class past ASCII, looked at: the next character is one
// the class leaves in, or the text ends.
void a_lookahead_on_a_complement(const cursiv::Parser& p) {
  const std::string acute(kAcute), lone(kLone), upper = "\xCE\x9B",
                                                lower = "\xCE\xBB";
  check(p, "xa:x", "x");
  check(p, with("xa:x", acute), "x|" + acute);
  check(p, "xa:xe", "<refused>");
  check(p, with("xa:x", lone), "<refused>");
  check(p, "xa:x\xC3", "<refused>");
  check(p, "xo:xe", "x|e");
  check(p, with("xo:x", lone), "x|" + lone);
  check(p, "xo:x\xC3", "x|\xC3");
  check(p, with("xo:x", acute), "<refused>");
  check(p, "xo:x", "<refused>");
  check(p, "xu:x", "x");
  check(p, with("xu:x", acute), "x|" + acute);
  check(p, "xu:xA", "x|A");
  check(p, with("xu:x", upper), "x|" + upper);
  check(p, "xu:xa", "<refused>");
  check(p, with("xu:x", lower), "<refused>");
  check(p, with("xu:x", lone), "<refused>");
  check(p, "xg:x", "x");
  check(p, with("xg:x", acute), "x|" + acute);
  check(p, "xg:xe", "<refused>");
  check(p, with("xg:x", lone), "<refused>");
  check(p, with("rn1:", acute), acute);
  check(p, "rn1:e", "<refused>");
  check(p, with("rn1:", lone), "<refused>");
  check(p, "rn2:e", "e");
  check(p, with("rn2:", lone), lone);
  check(p, with("rn2:", acute), "<refused>");
  check(p, "rn2:", "<refused>");
  check(p, with("first:", acute), acute);
  check(p, "first:e", "<refused>");
  check(p, with("first:", lone), "<refused>");
  check(p, "first:", "<refused>");
}

// A run closes at a character boundary, and a mark names whole characters.
void a_run_and_a_mark_read_characters(const cursiv::Parser& p) {
  check(p, "thr:<a\xA9", "<a\xA9");
  check(p, "thr:<\xC3\xA9\xA9", "<\xC3\xA9\xA9");
  check(p, "thr:<\xC3\xA9", "<refused>");
  check(p, "thr:<\xE2\x82\xA9", "<refused>");
  check(p, "here:<<ab\nx\nab", "<<ab\nx\nab");
  check(p, "here:<<ab\nx\nax", "<refused>");
  check(p, "here:<<\xC3\xA9\nbody\n\xC3\xA9", "<<\xC3\xA9\nbody\n\xC3\xA9");
  check(p, "here:<<\xFF\nb\n\xFF", "<<\xFF\nb\n\xFF");
  check(p, "here:<<\xC3\nb\n\xC3\xA9", "<refused>");
  check(p, "run:<\xC3\xA9\xC3\xA9>x<\xC3\xA9\xC3\xA9>",
        "<\xC3\xA9\xC3\xA9>x<\xC3\xA9\xC3\xA9>");
  check(p, "run:<\xC3\xA9\xC3\xA9>x<\xC3\xA9>", "<refused>");
}

// The keyword stands where no character of the boundary follows; a stray byte
// is one, for a boundary that is a complement.
void a_complement_boundary_holds_a_stray_byte(const cursiv::Parser& p) {
  const auto items = [&](std::string_view text) {
    const std::string source(text);
    const cursiv::ParseResult r = p.parse(source);
    const auto* const doc = r.value_as<tchars::ast::Doc>();
    if (!accepted(r) || doc == nullptr) return std::string("<refused>");
    std::string out;
    for (const tchars::ast::Item* const item : doc->items) {
      if (!out.empty()) out += '|';
      out += item->text;
    }
    return out;
  };
  const auto expect = [&](std::string_view text, std::string_view want) {
    const std::string got = items(text);
    if (got != want)
      std::printf("  [%s] %.*s\n    got      %s\n    expected %.*s\n", engine,
                  static_cast<int>(text.size()), text.data(), got.c_str(),
                  static_cast<int>(want.size()), want.data());
    CHECK(got == want);
  };
  expect("if", "if");
  expect("if x", "if|x");
  expect("if;", "if|;");
  expect("ifx", "ifx");
  expect("if\xFF", "if\xFF");
  expect("if\xC3(", "if\xC3(");
  expect("if\xC3\xA9", "if\xC3\xA9");
  expect("if\xE2\x82", "if\xE2\x82");
  expect("if \xFF", "if|\xFF");
}

void exercise(const cursiv::Parser& p) {
  whole_scalars(p);
  quotes(p);
  a_stray_byte_stands_alone(p);
  a_byte_is_one_byte(p);
  classes(p);
  the_scalar_and_the_byte(p);
  a_word_boundary_reads_a_character(p);
  a_run_and_a_mark_read_characters(p);
  a_lookahead_on_a_complement(p);
  any_text(p);
}

}  // namespace

void run() {
  engine = "LL";
  exercise(*tchars::generated::Ll::create_parser());
  gaps(*tchars::generated::Ll::create_parser());
  a_complement_boundary_holds_a_stray_byte(
      *tchars::generated::BoundsLl::create_parser());
  engine = "LR";
  exercise(*tchars::generated::Lr::create_parser());
  gaps(*tchars::generated::Lr::create_parser());
  a_complement_boundary_holds_a_stray_byte(
      *tchars::generated::BoundsLr::create_parser());

}

}  // namespace characters_cases

namespace repeated_unicode_cases {

namespace {

void check(const cursiv::Parser& parser, std::string_view source,
           bool expected) {
  const std::string input = "unicode:" + std::string(source);
  const auto result = parser.parse(input);
  const auto* node = result.value_as<trepeatedunicode::Node>();
  const bool clean = result.accepted && !result.cst().recovered() &&
                     result.diagnostics.empty() && node;
  CHECK(clean == expected);
  if (expected && node)
    CHECK(node->text == source.substr(source.find(':') + 1));
}

void exercise(const cursiv::Parser& parser) {
  for (const std::string_view letter : {"a", "é", "日", "𝒳"}) {
    check(parser, "scalar:" + std::string(letter), true);
    check(parser, "word:" + std::string(letter) + "_42", true);
    check(parser, "alias:@" + std::string(letter) + "_42", true);
    check(parser, "guard:" + std::string(letter), true);
    check(parser, "capture:==" + std::string(letter) + ":==", true);
    check(parser, "guard:" + std::string(letter) + "x", false);
    check(parser, "capture:==" + std::string(letter) + ":=", false);
  }
  // A combining mark continues an identifier but cannot start one.
  check(parser, "word:e\xCC\x81t\xC3\xA9", true);
  check(parser, "scalar:\xCC\x81", false);
  check(parser, "word:\xCC\x81" "e", false);
  check(parser, "complement:!", true);
  check(parser, "complement:🦀", true);
  check(parser, "complement:日", false);
  check(parser, "complement:7", false);
  check(parser, "scalar:7", false);
  check(parser, "scalar:🦀", false);
  for (const std::string_view blank :
       {"\t", "\f", "\xc2\x85", "\xc2\xa0", "\xe1\x9a\x80",
        "\xe2\x80\x80\xe2\x80\x8a", "\xe2\x80\xa8\xe2\x80\xa9", "\xe3\x80\x80",
        "\xe2\x81\x9f\xe2\x80\xaf"})
    check(parser, "space:" + std::string(blank), true);
  for (const std::string_view other :
       {"\x1c", "\xe2\x80\x8b", "\xe1\xa0\x8e", "\xef\xbb\xbf", "\xe2\x81\xa0",
        "x", "\xc2"})
    check(parser, "space:" + std::string(other), false);
  // A byte that starts no scalar is no letter, and a character of the
  // complement: one at a time.
  for (const std::string_view broken :
       {"\xc3", "\xe6\x97", "\xf0\x9f\xa6", "\x80"}) {
    for (const std::string_view prefix : {"scalar:", "word:"})
      check(parser, std::string(prefix) + std::string(broken), false);
    check(parser, "complement:" + std::string(broken), broken.size() == 1);
  }
}

}  // namespace

void run() {
  exercise(*tchars::generated::Ll::create_parser());
  exercise(*tchars::generated::Lr::create_parser());

}

}  // namespace repeated_unicode_cases

namespace unicode_lexical_cases {
// A class lexes exactly the scalars it names, in well-formed UTF-8, and a
// complement also the bytes that start no scalar.

namespace {

// The shared word reader keeps the grammar's XID_Continue | "$" boundary,
// including refusals and an incomplete UTF-8 scalar.
void the_word_class_obeys_the_declared_boundary() {
  for (const auto& [text, word] : {std::pair<std::string_view, bool>{"a", true},
                                   {"$", true},
                                   {"\xC3\xA9", true},
                                   {"\xE6\x97\xA5", true},
                                   {"\xF0\x9D\x92\xB3", true},
                                   {"\xC2\xAC", false},
                                   {"\xC3", false},
                                   {"!", false}}) {
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    CHECK(cursiv::lex_boundary(tchars::generated::Lr::kTable.cfg)
              .word_at(text.data(), 0, len) == word);
  }
}

const cursiv::Parser& wide() {
  static const auto p = tchars::generated::Lr::create_parser();
  return *p;
}

// A case's document: its opening keyword, then what the case reads.
std::string in(std::string_view lead, std::string_view text) {
  return "lex:" + std::string(lead) + " " + std::string(text);
}

std::vector<std::string> dot_items(const cursiv::Parser& p,
                                   std::string_view text) {
  const cursiv::ParseResult r = p.parse(text);
  if (!r.accepted || !r.diagnostics.empty()) return {"<refused>"};
  std::vector<std::string> out;
  for (const tunicode::ast::Item* const it :
       r.value_as<tunicode::ast::Doc>()->items)
    out.emplace_back(it->name);
  return out;
}

// The guarded scalar acceptances are cases of the shared wide table.
bool dot(std::string_view text, std::vector<std::string> want) {
  return dot_items(wide(), in("dot", text)) == want;
}

bool cased(std::string_view text, std::vector<std::string> want) {
  const std::string document = in("cased", text);
  const auto result = wide().parse(document);
  std::vector<std::string> got;
  if (!result.accepted || !result.diagnostics.empty()) return want == std::vector<std::string>{"<refused>"};
  for (const auto* item : result.value_as<tunicode::ast::Doc>()->items)
    got.emplace_back(item->cond ? "<if>" : item->where ? "<où>" : std::string(item->name));
  return got == want;
}

// The names of a clean reading of a document; `<refused>` otherwise.
bool names(const cursiv::Parser& p, const std::string& doc,
           std::vector<std::string> want) {
  const cursiv::ParseResult r = p.parse(doc);
  std::vector<std::string> got;
  if (!r.accepted || !r.diagnostics.empty()) {
    got = {"<refused>"};
  } else {
    for (const tunicode::ast::Item* const it :
         r.value_as<tunicode::ast::Doc>()->items)
      got.emplace_back(it->name);
  }
  if (got == want) return true;
  std::printf("  %s ->", doc.c_str());
  for (const std::string& g : got) std::printf(" [%s]", g.c_str());
  std::printf("\n");
  return false;
}

}  // namespace

int run() {
  cursiv::test::count_expectations();
  the_word_class_obeys_the_declared_boundary();
  const auto& parser = wide();
  for (const std::string_view digits : {"42", "٣٤", "๑"})
    characters_cases::check(parser, "digits:" + std::string(digits), digits);
  characters_cases::check(parser, "digits:a", "<refused>");
  characters_cases::check(parser, "kwt:if$", "<refused>");
  characters_cases::check(parser, "where:où", "où");
  characters_cases::check(parser, "where:où€", "où|€");
  characters_cases::check(parser, "where:oùx", "<refused>");
  characters_cases::check(parser, "where:oùé", "<refused>");
  // A guard of code points reads the scalar behind the match, at every
  // encoding length.
  CHECK(dot("1.", {"1."}));
  CHECK(dot("1.2", {"1.2"}));
  CHECK(dot("1. x", {"1.", "x"}));
  CHECK(dot("1.x", {"1", ".", "x"}));
  CHECK(dot("1.\xC3\xA9", {"1", ".", "\xC3\xA9"}));
  CHECK(dot("1.\xE6\x97\xA5", {"1", ".", "\xE6\x97\xA5"}));
  CHECK(dot("1.\xF0\x9D\x92\xB3", {"1", ".", "\xF0\x9D\x92\xB3"}));
  CHECK(dot("1..2", {"1", "..", "2"}));
  // `_` starts a name in the grammar but is no XID_Start: the guard says so.
  CHECK(dot("1._", {"1.", "_"}));
  CHECK(dot("#1", {"#", "1"}));
  CHECK(dot("#_", {"#_"}));
  CHECK(dot("# \xC3\xA9", {"#", "\xC3\xA9"}));
  CHECK(dot("#x", {"<refused>"}));
  CHECK(dot("#\xC3\xA9", {"<refused>"}));
  CHECK(dot("#\xE6\x97\xA5", {"<refused>"}));
  CHECK(dot("#\xF0\x9D\x92\xB3", {"<refused>"}));
  CHECK(dot("# \xF0\x9D\x92\xB3", {"#", "\xF0\x9D\x92\xB3"}));
  // Refused by its class, the first terminal leaves the spelling to the
  // second where that one's class allows it, at every encoding length.
  CHECK(cased("a", {"<if>"}));
  CHECK(cased("a B", {"<if>", "B"}));
  CHECK(cased("aB", {"<où>", "B"}));
  CHECK(cased("a\xC3\x80", {"<où>", "\xC3\x80"}));
  CHECK(cased("a\xCE\xA3", {"<où>", "\xCE\xA3"}));
  CHECK(cased("a\xF0\x9D\x90\x80", {"<où>", "\xF0\x9D\x90\x80"}));
  CHECK(cased("a\xC3\xA9", {"<if>", "\xC3\xA9"}));
  // The boundary inside a token: the token goes on past `¬`, never past a
  // scalar the class holds.
  CHECK(names(wide(), in("inside", "if¬ if¬"), {"if¬", "if¬"}));
  CHECK(names(wide(), in("inside", "ifé"), {"<refused>"}));
  CHECK(names(wide(), in("inside", "if"), {"<refused>"}));
  // The boundary joined by a byte class: still the class past ASCII.
  CHECK(names(wide(), in("trailing", "if¬ if"), {"if", "¬", "if"}));
  CHECK(names(wide(), in("trailing", "if €"), {"if", "€"}));
  CHECK(names(wide(), in("trailing", "if!"), {"<refused>"}));
  CHECK(names(wide(), in("trailing", "ifé"), {"<refused>"}));
  // Two reservations of one word: the name comes back where both refuse it.
  CHECK(names(wide(), in("intersect", "ifé"), {"if"}));
  CHECK(names(wide(), in("intersect", "ifé abé"), {"if", "ab"}));
  CHECK(names(wide(), in("intersect", "if é"), {"<refused>"}));
  // A guard refusing every scalar past ASCII beside a keyword of the same
  // word: the keyword stands before `€`, and the name before `·`, a word.
  // Where the guard passes, the literal and the keyword tie, and the rule's
  // first arm, the literal's, is elected.
  {
    static constexpr auto& A = tchars::generated::Lr::kTable;
    std::uint16_t mode = 0xFFFF;
    for (unsigned s = 0; s < A.nstates; ++s) {
      const auto t = A.derived.expect.shiftTerm[s];
      if (t < A.cfg.nterms && A.cfg.term_text(t) == "rawlead") mode = A.mode[s];
    }
    CHECK(mode != 0xFFFF);
    const auto at = [&](std::string_view t) {
      const cursiv::Scan s = cursiv::lr_match_text<A>(
          t.data(), static_cast<cursiv::TextOffset>(t.size()), mode, 0);
      return s.term == cursiv::kInvalidSymbol
                 ? std::string("<none>")
                 : std::string(A.cfg.term_name(s.term)) + "@" +
                       std::to_string(s.end);
    };
    CHECK(at("if\xE2\x82\xAC") == "K@2");
    CHECK(at("if\xC2\xB7") == "ID@2");
    CHECK(at("if!") == "P@2");
    CHECK(names(wide(), in("rawlead", "if\xE2\x82\xAC"), {"", "\xE2\x82\xAC"}));
    CHECK(names(wide(), in("rawlead", "if\xC2\xB7"), {"if", "\xC2\xB7"}));
    CHECK(names(wide(), in("rawlead", "if!"), {"if", "!"}));
  }
  // `\xNN` spells one scalar, in a class of ASCII or beside a letter past it.
  CHECK(names(wide(), in("escape", "b:ABC u:CAB"), {"b:ABC", "u:CAB"}));
  CHECK(names(wide(), in("escape", "u:A\u00e9B"), {"u:A\u00e9B"}));
  for (const char* t : {"u:x41", "u:5", "u:w", "u:x", "b:x41"})
    CHECK(names(wide(), in("escape", t), {"<refused>"}));
  return 0;
}
}  // namespace unicode_lexical_cases

int main() {
  cursiv::test::count_expectations();
  characters_cases::run();
  repeated_unicode_cases::run();
  unicode_lexical_cases::run();
  return cursiv::test::report("Characters_test");
}
