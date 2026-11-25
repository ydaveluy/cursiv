#include "cursiv/parser/Parser.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Split.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/LexicalBackendsAst.hpp"
#include "lexicalbackendslr/generated/Suite/Module.hpp"
#include "lexicalbackendslr/generated/Suite/Table.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lexical_backends_cases {

namespace {
using Items = std::vector<std::string>;
void check(const cursiv::Parser& parser, std::string_view text,
           const Items& expected) {
  const auto r = parser.parse(text);
  const auto* d = r.valueAs<tlexbackends::Document>();
  const bool clean = r.accepted && r.diagnostics.empty() && d;
  if (!clean)
    std::fprintf(stderr, "lexical backend rejected: %.*s\n",
                 static_cast<int>(std::min<std::size_t>(text.size(), 100)),
                 text.data());
  CHECK(clean);
  if (d) CHECK(Items(d->items.begin(), d->items.end()) == expected);
}
void rejects(const cursiv::Parser& parser, std::string_view text) {
  const auto r = parser.parse(text);
  CHECK(!r.accepted || !r.diagnostics.empty());
}

void retained_readings_keep_their_ties() {
  using namespace cursiv::engine;
  const auto& tables = tlexbackends::generated::Suite::kTables;
  const std::string_view source = "ns tag:i";
  cursiv::CstRun tape;
  Machine shared(tables, source, tape, cursiv::kNoCancel);
  std::vector<std::pair<std::uint32_t, Token>> saved;
  for (std::uint32_t mode = 0; mode < tables.modes.size(); ++mode)
    if (const Reader read = tables.modes[mode].read)
      saved.emplace_back(mode, read(shared, shared.src + 3, mode));

  std::set<std::vector<std::uint16_t>> groups;
  for (const auto& [mode, token] : saved) {
    Machine fresh(tables, source, tape, cursiv::kNoCancel);
    const auto expected = tables.modes[mode].read(fresh, fresh.src + 3, mode);
    CHECK(token.begin(shared) == expected.begin(fresh));
    CHECK(token.terminal() == expected.terminal() && token.end == expected.end);
    CHECK(token.hasGroup() == expected.hasGroup());
    std::vector<std::uint16_t> members;
    for (std::uint16_t term = 0; term < tables.terminalCount; ++term) {
      const bool member = fresh.reads_tied(expected, term);
      CHECK(shared.reads_tied(token, term) == member);
      if (member && expected.hasGroup()) members.push_back(term);
    }
    if (!members.empty()) groups.insert(std::move(members));
  }
  CHECK(groups.size() >= 2);
}

// Words two terminals read alike are told by the token after each, with no
// reading ahead.
void tied_words_are_told_by_the_next_token(const cursiv::Parser& parser) {
  using namespace cursiv::engine;
  std::string text = "tied";
  for (unsigned i = 0; i < 64; ++i) text += " ab( cd=";
  Items items;
  for (unsigned i = 0; i < 64; ++i) items.insert(items.end(), {"ab", "cd"});
  check(parser, text, items);
  const auto& t = tlexbackends::generated::Suite::kTables;
  cursiv::Cst cst(text);
  cursiv::CstBuilder builder(cst);
  Machine m(t, cst.text(), builder.stream(), cursiv::kNoCancel);
  bool retry = false;
  CHECK(run(m, &retry) == Exit::accepted && !retry);
  CHECK(m.explored == 0);
}

void scanners(const cursiv::Parser& parser) {
  // A scan written in a rule is a token of its own.
  check(parser, "scan:bare:D0D0", {"D0", "D0"});
  const std::string_view items = "scan:one:D0";
  const auto parsed = parser.parse(items);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(parsed.cst(), items.size(), items.size(), next);
  CHECK(std::ranges::any_of(next, [](const auto& f) { return f.property == "items"; }));
  const std::string_view adjacent = "scan:adjacent:abc D0";
  const auto separated = parser.parse(adjacent);
  CHECK(separated.accepted && separated.diagnostics.empty());
  const auto blank = adjacent.find(' ');
  for (const auto at : {blank, blank + 1}) {
    next.clear();
    parser.nextFeaturesAt(separated.cst(), at, adjacent.size(), next);
    CHECK(std::ranges::any_of(next, [](const auto& f) { return f.property == "items"; }) == (at > blank));
  }
  const std::string_view alternate = "scan:alternate:abc D0";
  const auto prefix = parser.parse(alternate);
  CHECK(prefix.accepted && prefix.diagnostics.empty());
  next.clear();
  parser.nextFeaturesAt(prefix.cst(), alternate.find(' '), alternate.size(), next);
  CHECK(std::ranges::any_of(next, [](const auto& f) { return f.property == "items"; }));
  for (const std::string_view choice : {"scan:choose:: ab!", "scan:comma::, ab!", "scan:comma::, ab?"}) {
    const auto document = parser.parse(choice);
    CHECK(document.accepted && document.diagnostics.empty());
    const auto at = choice.rfind("ab");
    next.clear();
    parser.nextFeaturesAt(document.cst(), at, at + 1, next);
    CHECK(std::ranges::any_of(next, [&](const auto& f) {
      const auto words = parser.getGrammar().terminalSpellings(f.feature);
      return std::ranges::find(words, "a") != words.end();
    }) == choice.ends_with('!'));
  }
  check(parser, "scan:rank:tag:", {"tag"});
  for (const auto prefix : {"scan:pair:tag", "scan:reverse:tag"})
    for (const auto suffix : {':', '!'}) check(parser, std::string(prefix) + suffix, {"tag"});
  check(parser, "scan:mixed:tag:", {"tag"});
  check(parser, "scan:mixed:tag!m", {"tag"});
  check(parser, "scan:mixed:tag!i", {});
  check(parser, "scan:mixed:name:", {});
  check(parser, "scan:mixedtie:tag:i", {});
  check(parser, "scan:mixedtie:tag:m", {"tag"});
  for (const std::string_view source : {"scan:held:D2aa!!", "scan:held:D2aa;"}) {
    const auto result = parser.parse(source);
    CHECK(result.accepted && result.diagnostics.empty());
    next.clear();
    parser.nextFeaturesAt(result.cst(), 14, source.size(), next);
    std::vector<std::string_view> words;
    for (const auto& f : next)
      for (auto word : parser.getGrammar().terminalSpellings(f.feature)) words.push_back(word);
    std::ranges::sort(words);
    CHECK((words == std::vector<std::string_view>{"!!", ";"}));
  }
  const std::string_view keys = "scan:keys:";
  for (std::string_view end : {":", "abc:"}) {
    const std::string source = std::string(keys) + std::string(end);
    const auto document = parser.parse(source);
    next.clear();
    parser.nextFeaturesAt(document.cst(), keys.size(), source.size() - 1, next);
    CHECK(std::ranges::any_of(next, [](const auto& f) { return f.property == "items"; }));
  }
  for (const auto mode : {"one:", "prefix:", "four:"})
    check(parser, std::string("scan:") + mode + "D5abcdeD2aaD0", {"D5abcde", "D2aa", "D0"});
  check(parser, "scan:four:A0B0C0D0", {"A0", "B0", "C0", "D0"});
  check(parser, "scan:prefix:A0B0C0D0", {"A0", "B0", "C0", "D0"});
  check(parser, "scan:probe:Z1x", {"Z1x"});
  Items tokens(32768, "D0");
  std::string frames;
  frames.reserve(2 * tokens.size());
  for (const auto& token : tokens) frames += token;
  for (const std::string mode : {"scan:retry:", "scan:either:"}) {
    check(parser, mode + frames, tokens);
    check(parser, mode + frames + ";D0", {frames + ";", "D0"});
  }
  check(parser, "scan:loop:" + frames, tokens);
  check(parser, "scan:loop:" + frames + "A0D0", {frames + "A0", "D0"});
  check(parser, "scan:run:D0D2aa;", {"D0D2aa;"});
  check(parser, "scan:optional:D0D1x;", {"D0D1x;"});
  check(parser, "scan:optional:;", {";"});
  check(parser, "scan:suffix:D1;;", {"D1;;"});
  const std::string binary("\0" "1\xff", 3);
  check(parser, "scan:binary:" + binary, {binary});
  check(parser, "scan:delimited:[xD1;", {"[xD1;"});
  check(parser, "scan:positive:@D1x", {"@D1x"});
  check(parser, "scan:negative:D5abc", {"D5abc"});
  check(parser, "scan:tie:D1x!", {"D1x"});
  check(parser, "scan:tie:D1x?", {"D1x"});
  check(parser, "scan:tie:D1xlong?", {"D1xlong"});
  check(parser, "scan:tie:D1-!", {"D1-"});
  check(parser, "scan:literal:D0?", {});
  for (auto text : {"one:", "one:D5abc", "prefix:D", "prefix:D5abc", "suffix:D1;",
                    "positive:@D5abc", "negative:D1x", "invalid:x", "probe:Z5abc", "run:;", "tie:D1xlong!", "literal:D0!"})
    rejects(parser, std::string("scan:") + text);
}
void exercise(const cursiv::Parser& parser) {
  for (const std::string raw : {"#\"a\"b\"#", "##\"a\"#b\"##", "#\"\"#"})
    check(parser, "swift " + raw, {raw});
  check(parser, "pair #\"first\"# ; ##\"second\"##",
        {"#\"first\"#", "##\"second\"##"});
  check(parser, "suffix ##\"a\"#b\"##tail", {"##\"a\"#b\"##tail"});
  check(parser, "cpp R\"a\"b(content)a\"b\"", {"R\"a\"b(content)a\"b\""});
  check(parser, "cpp R\"(empty delimiter)\"", {"R\"(empty delimiter)\""});
  check(parser, "multiple ##:abc|##~abc!", {"##:abc|##~abc!"});
  check(parser, "overlap <<ABC\nbody\n  AAABC;", {"<<ABC\nbody\n  AAABC;"});
  check(parser, "empty <<\nbody\n   ", {"<<\nbody\n   "});
  check(parser, "after <<\nbody\n ; ;", {"<<\nbody\n ; ;"});
  check(parser, "fast [==[body]=]more]==]", {"[==[body]=]more]==]"});
  check(parser, "twin <abc-abc>", {"<abc-abc>"});
  for (const auto text : {"abcbdbc!", "abcbcbcbcbc!"})
    check(parser, "counted " + std::string(text), {text});
  for (const auto text : {"a!", "abcbd!", "abcbcbcbc!"})
    check(parser, "stopped " + std::string(text), {text});
  for (const auto text : {"n123_456!", "n_!", "n42!"})
    check(parser, "digits " + std::string(text), {text});
  check(parser, "chained 42", {"42"});
  check(parser, "nullable-guard nb", {"n"});
  check(parser, "nullable-guard nab", {"na"});
  check(parser, "empty-guard e", {"e"});
  check(parser, "cyclic-guard ca", {"ca"});
  check(parser, "guarded-raw ##\"body\"##", {"##\"body\"##"});
  for (auto invalid :
       {"swift ##\"x\"#", "suffix #\"x\"#wrong", "cpp R\"a\"b(x)a\"c\"",
        "multiple ##:abc|#~abc!", "multiple ##:abc|##~abd!",
        "overlap <<ABC\nbody\n  AB;", "after <<\nbody\n", "twin <abc-abd>",
        "counted abcbc!", "counted abcbcbcbcbcbc!", "counted abcbdbx!",
        "stopped ab!", "stopped abcbcbcbcbc!", "digits nx!", "digits n12é!",
        "digits n123_456x", "chained abc", "chained Z", "chained 42a",
        "nullable-guard n42xb", "empty-guard e42", "cyclic-guard cb",
        "guarded-raw ##\"body\"#"})
    rejects(parser, invalid);

  const auto begin = std::chrono::steady_clock::now();
  const std::string ordinary = "#\"" + std::string(250000, 'x') + "\"#";
  check(parser, "swift " + ordinary, {ordinary});
  const std::string hashes = "#\"" + std::string(8192, '#') + "\"#";
  check(parser, "swift " + hashes, {hashes});
  const std::string quotes = "##\"" + std::string(100000, '\"') + "\"##";
  check(parser, "swift " + quotes, {quotes});
  std::printf(
      "  250k body, 8192 hashes and 100k quotes: %.3fs\n",
      std::chrono::duration<double>(std::chrono::steady_clock::now() - begin)
          .count());
}
}  // namespace

}  // namespace lexical_backends_cases

namespace lexical_captures_cases {

namespace {
void check(const cursiv::Parser& parser, std::string_view source,
           bool expected) {
  const std::string input = "capture:" + std::string(source);
  const auto result = parser.parse(input);
  const auto* node = result.valueAs<tcaptures::Node>();
  const bool clean = result.accepted && result.diagnostics.empty() && node;
  if (clean != expected)
    std::printf("capture input=%.*s expected=%d actual=%d\n",
                static_cast<int>(std::min<std::size_t>(source.size(), 120)),
                source.data(), expected, clean);
  CHECK(clean == expected);
  if (clean && expected)
    CHECK(node->text == source.substr(source.find(':') + 1));
}

void exercise(const cursiv::Parser& parser) {
  for (const auto source :
       {"branch:##:##!", "branch:==~==!",
        "guard:##|##!", "negative:##|##!", "own:==:==!", "local:##|==:==:##!",
        "shadow:##|==:==##!", "redefined:#=@=@!", "helper:##|(==:==)|##!",
        "recursive:##|(==:==)|##!", "recursive:##|(==:==)/(=:=)|##!",
        "inline:##|(==:==)|==!", "nested:##|(=:(==::==):=)|##!",
        "probe:<==:==>", R"(raw:#"text"#)", R"(raw:##"a\##"b"##)",
        R"(raw:##"a\##(#"inner"#)b"##)",
        R"(raw:#"a\#(##"inner\##"quote"##)b"#)"})
    check(parser, source, true);
  for (const auto source :
       {"branch:##:#!",
        "branch:==~=!", "guard:##|#!", "negative:##|##x!", "own:==:=!",
        "local:##|==:=:##!", "shadow:##|==:=##!", "redefined:#=@=#!",
        "helper:##|(==:==)|==!", "recursive:##|(==:==)|==!",
        "inline:##|(==:==)|##!", "nested:##|(=:(==::=):=)|##!", "probe:<==:=>",
        R"(raw:##"a\##"b"#)"})
    check(parser, source, false);
  const std::string plain = "raw:##\"" + std::string(100000, 'x') + "\"##";
  check(parser, plain, true);
  const std::string hashes = "raw:##\"" + std::string(8192, '#') + "\"##";
  check(parser, hashes, true);
}
}  // namespace

}  // namespace lexical_captures_cases

namespace lexical_through_cases {

namespace {
void check(const cursiv::Parser& parser, std::string_view prefix,
           std::string_view body, bool expected = true,
           std::string_view suffix = {}) {
  const std::string source =
      std::string(prefix) + std::string(body) + std::string(suffix);
  const std::string input = "capture:" + source;
  const auto r = parser.parse(input);
  const auto* node = r.valueAs<tthrough::Node>();
  const bool clean =
      r.accepted && !r.cst().recovered() && r.diagnostics.empty() && node;
  if (clean != expected)
    std::fprintf(stderr, "through %.*s (%zu bytes): expected %d, got %d\n",
                 static_cast<int>(prefix.size()), prefix.data(), body.size(),
                 expected, clean);
  CHECK(clean == expected);
  if (clean && expected) CHECK(node->text == body);
}
void exercise(const cursiv::Parser& parser) {
  check(parser, "guarded:", "/* ENDING END");
  check(parser, "guarded:", "/*END");
  check(parser, "guarded:", "/*ENDING", false);
  check(parser, "manual:", "/**/");
  check(parser, "manual:", "/* *almost/ closed */");
  check(parser, "manual:", "/*unfinished", false);
  check(parser, "manual:", "/**/trailing*/", false);
  check(parser, "folded:", "[ender EnD");
  check(parser, "folded:", "[EnD");
  check(parser, "folded:", "[Ender", false);
  check(parser, "choice:", "[oneSTOP");
  check(parser, "choice:", "[twoEND");
  check(parser, "choice:", "[none", false);
  check(parser, "first:", "[oneSTOP", true, "tail");
  check(parser, "first:", "[twoEND", true, "tail");
  check(parser, "first:", "[three", true, "tail");
  check(parser, "first:", "[oneSTOPextraEND", false, "tail");
  check(parser, "endchoice:", "[unterminated");
  check(parser, "endchoice:", "[closedSTOP");
  check(parser, "end:", "[body");
  check(parser, "end:", "[");
  check(parser, "nullable:", "[");
  check(parser, "nullable:", "[]");
  check(parser, "nullable:", "[x]", false);
  for (auto prefix : {"heredoc:", "explicit:"}) {
    check(parser, prefix, "<<<TAG\nTAG");
    check(parser, prefix, "<<<TAG\nbody\n  TAG");
    check(parser, prefix, "<<<TAG\nTAGsuffix\nTAG_\nTAG2\nTAG");
    check(parser, prefix, "<<<TAG\nTAGsuffix", false);
    check(parser, prefix, "<<<TAG\nbody\nOTHER", false);
    check(parser, prefix, "<<<TAG\n" + std::string(100000, 'x') + "\nTAG");
  }
  check(parser, "padding:", "<<<TAG\nbody\n \t TAG");
  check(parser, "same:", "==:body==");
  check(parser, "same:", "=:body=");
  check(parser, "same:", "==:body=", false);
  check(parser, "empty:", ":");
  check(parser, "empty:", "#:body#");
  check(parser, "empty:", ":body", false);
  for (const auto prefix : {"char-fast:", "char-explicit:"}) {
    check(parser, prefix, "<=>\xA9=");
    for (const std::string scalar : {"\xC3\xA9", "\xE0\xAA\xA9",
                                     "\xF0\x90\x80\xA9"}) {
      check(parser, prefix, "<=>" + scalar + "=", false);
      check(parser, prefix, "<=>" + scalar + "=\xA9=");
    }
    for (const auto invalid : {"<=>\xC0\xA9=", "<=>\xED\xA0\xA9="})
      check(parser, prefix, invalid);
    const std::string mark(129, '=');
    check(parser, prefix, "<" + mark + ">\xC3\xA9" + mark, false);
    check(parser, prefix, "<" + mark + ">\xC3\xA9" + mark + "\xA9" + mark);
  }
  check(parser, "char-rec:", "=>\xA9=");
  check(parser, "char-rec:", "=>\xC3\xA9=", false);
  check(parser, "char-rec:", "=>\xC3\xA9=\xA9=");
  for (const auto prefix : {"char-pad:", "char-pad-explicit:",
                            "char-pad-rec:"}) {
    check(parser, prefix, "<=>\xA9 =");
    check(parser, prefix, "<=>\xC3\xA9 =", false);
    check(parser, prefix, "<=>\xC3\xA9 =\xA9 =");
  }
  check(parser, "char-mark:", "<\xA9>\xA9!");
  check(parser, "char-mark:", "<\xA9>\xC3\xA9!", false);
  check(parser, "char-mark:", "<\xA9>\xC3\xA9!\xA9!");
  check(parser, "char-split:", "<=\xC3\xA9=");
  check(parser, "writing:", "#[body]==:==");
  // A failed closing probe must not publish its new capture. The other
  // alternative still needs the original # mark, which survives afterwards.
  check(parser, "writing:", "#[body]==x}#:#");
  check(parser, "writing:", "#[body]==x}==:==", false);
}
}  // namespace

}  // namespace lexical_through_cases

namespace rule_through_cases {

namespace {

const trulethrough::Node* read(const cursiv::ParseResult& r) {
  const bool clean =
      r.accepted && !r.cst().recovered() && r.diagnostics.empty();
  return clean ? r.valueAs<trulethrough::Node>() : nullptr;
}

void text(const cursiv::Parser& parser, std::string_view source,
          std::string_view expected) {
  const std::string input = "rule:" + std::string(source);
  const auto r = parser.parse(input);
  const auto* node = read(r);
  if (node == nullptr || node->text != expected)
    std::fprintf(stderr, "rule through %.*s: expected a node with %.*s\n",
                 static_cast<int>(source.size()), source.data(),
                 static_cast<int>(expected.size()), expected.data());
  CHECK(node != nullptr);
  if (node != nullptr) CHECK(node->text == expected);
}

void refused(const cursiv::Parser& parser, std::string_view source) {
  const std::string input = "rule:" + std::string(source);
  const auto r = parser.parse(input);
  if (read(r) != nullptr)
    std::fprintf(stderr, "rule through %.*s: expected a refusal\n",
                 static_cast<int>(source.size()), source.data());
  CHECK(read(r) == nullptr);
}

void exercise(const cursiv::Parser& parser) {
  text(parser, "block /* a  b */", "/* a  b */");
  text(parser, "block\n/*\n*/", "/*\n*/");
  refused(parser, "block /* a */ b");
  refused(parser, "block /* unfinished");

  text(parser, "open /* x */", "/* x */");
  text(parser, "open /* unfinished", "/* unfinished");

  text(parser, "first <a> x", "<a>");
  refused(parser, "first <a> y> x");

  text(parser, "bracket [[x]]", "[[x]]");
  text(parser, "bracket [==[a]=]b]==]", "[==[a]=]b]==]");
  refused(parser, "bracket [=[x]==]");

  text(parser, "either 'q'", "'q'");
  text(parser, "either abc", "abc");
  refused(parser, "either 'q");

  const auto many = parser.parse("rule:many 'a' 'b'   'c'");
  const auto* node = read(many);
  CHECK(node != nullptr);
  if (node != nullptr) {
    CHECK(node->parts.size() == 3);
    if (node->parts.size() == 3) {
      CHECK(node->parts[0] == "'a'");
      CHECK(node->parts[1] == "'b'");
      CHECK(node->parts[2] == "'c'");
    }
  }
  CHECK(read(parser.parse("rule:many")) != nullptr);
  refused(parser, "many 'a' 'b");

  text(parser, "peek <a>", "<a>");
  refused(parser, "peek <a");

  text(parser, "unless ac", "ac");
  text(parser, "unless abc", "abc");
  refused(parser, "unless ab");
}

}  // namespace

}  // namespace rule_through_cases

int main() {
  cursiv::test::countExpectations();
  const auto parser = tlexbackends::generated::Suite::createParser();
  lexical_backends_cases::exercise(*parser);
  lexical_backends_cases::scanners(*parser);
  lexical_backends_cases::retained_readings_keep_their_ties();
  lexical_backends_cases::tied_words_are_told_by_the_next_token(*parser);
  lexical_captures_cases::exercise(*parser);
  lexical_through_cases::exercise(*parser);
  rule_through_cases::exercise(*parser);
  return cursiv::test::report("LexicalBackends_test");
}
