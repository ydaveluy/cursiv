#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/shared/runtime/LexicalMachine.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/LexicalBackendsGrammar.hpp"
#include "lexicalbackendsll/generated/Ll/Module.hpp"
#include "lexicalbackendsll/generated/Ll/Table.hpp"
#include "lexicalbackendslr/generated/Lr/Module.hpp"
#include "lexicalbackendslr/generated/Lr/Table.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace lexical_backends_cases {

namespace {
using Items = std::vector<std::string>;
void check(const cursiv::Parser& parser, std::string_view text,
           const Items& expected) {
  const auto r = parser.parse(text);
  const auto* d = r.value_as<tlexbackends::Document>();
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
template <class Table>
std::string_view term_name(const Table& table, std::uint16_t term) {
  if constexpr (requires { table.cfg.term_name(term); })
    return table.cfg.term_name(term);
  else
    return table.term_name(term);
}
template <class Table>
void selected_backends(const Table& table) {
  for (auto term :
       {"Swift", "Suffix", "Cpp", "Multiple", "Overlap", "Empty", "After"}) {
    bool found = false;
    for (unsigned i = 0; i < table.nrelays; ++i)
      if (term_name(table, table.relays[i].term) == term) {
        found = true;
        CHECK(table.relays[i].recognizes != nullptr);
      }
    CHECK(found);
  }
  for (auto term : {"Fast", "Twin"}) {
    bool found = false;
    for (unsigned i = 0; i < table.nrelays; ++i)
      if (term_name(table, table.relays[i].term) == term) {
        found = true;
        CHECK(table.relays[i].recognizes == nullptr);
      }
    CHECK(found);
  }
}
void exercise(const cursiv::Parser& parser, const char* engine) {
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
  for (auto invalid :
       {"swift ##\"x\"#", "suffix #\"x\"#wrong", "cpp R\"a\"b(x)a\"c\"",
        "multiple ##:abc|#~abc!", "multiple ##:abc|##~abd!",
        "overlap <<ABC\nbody\n  AB;", "after <<\nbody\n", "twin <abc-abd>"})
    rejects(parser, invalid);

  const auto begin = std::chrono::steady_clock::now();
  const std::string ordinary = "#\"" + std::string(250000, 'x') + "\"#";
  check(parser, "swift " + ordinary, {ordinary});
  const std::string hashes = "#\"" + std::string(8192, '#') + "\"#";
  check(parser, "swift " + hashes, {hashes});
  const std::string quotes = "##\"" + std::string(100000, '\"') + "\"##";
  check(parser, "swift " + quotes, {quotes});
  std::printf(
      "  %s: 250k body, 8192 hashes and 100k quotes: %.3fs\n", engine,
      std::chrono::duration<double>(std::chrono::steady_clock::now() - begin)
          .count());
}
}  // namespace
void run() {
  selected_backends(tlexbackends::generated::Lr::kTable);
  selected_backends(tlexbackends::generated::Ll::kTable);
  exercise(*tlexbackends::generated::Lr::create_parser(), "LR");
  exercise(*tlexbackends::generated::Ll::create_parser(), "LL");

}

}  // namespace lexical_backends_cases

namespace lexical_captures_cases {

namespace {
void check(const cursiv::Parser& parser, std::string_view source,
           bool expected) {
  const std::string input = "capture:" + std::string(source);
  const auto result = parser.parse(input);
  const auto* node = result.value_as<tcaptures::Node>();
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
       {"multi:##:===|##~===!", "branch:##:##!", "branch:==~==!",
        "guard:##|##!", "negative:##|##!", "own:==:==!", "local:##|==:==:##!",
        "shadow:##|==:==##!", "redefined:#=@=@!", "helper:##|(==:==)|##!",
        "recursive:##|(==:==)|##!", "recursive:##|(==:==)/(=:=)|##!",
        "inline:##|(==:==)|==!", "nested:##|(=:(==::==):=)|##!",
        "probe:<==:==>", R"(raw:#"text"#)", R"(raw:##"a\##"b"##)",
        R"(raw:##"a\##(#"inner"#)b"##)",
        R"(raw:#"a\#(##"inner\##"quote"##)b"#)"})
    check(parser, source, true);
  for (const auto source :
       {"multi:##:===|#~===!", "multi:##:===|##~==!", "branch:##:#!",
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

void states() {
  static_assert(sizeof(cursiv::PredicateCursor) == 16);
  static_assert(sizeof(cursiv::LexicalCursor) == 8);
  const std::array<cursiv::PredicateCursor, 6> input{{{4, 1, 2, 3, 8},
                                                      {4, 0, 2, 3, 8},
                                                      {4, 1, 1, 3, 8},
                                                      {4, 1, 2, 2, 8},
                                                      {4, 1, 2, 3, 7},
                                                      {3, 9, 9, 9, 9}}};
  cursiv::PredicateState state;
  for (auto cursor : input) {
    state.add(cursor);
    state.add(cursor);
  }
  CHECK(state.size() == input.size());
  for (auto cursor : input) CHECK(state.has(cursor));
  std::vector<cursiv::PredicateCursor> order(input.begin() + 1, input.end());
  std::sort(order.begin(), order.end(), [](auto a, auto b) {
    return std::tie(a.at, a.from, a.held, a.mode, a.end) <
           std::tie(b.at, b.from, b.held, b.mode, b.end);
  });
  for (std::size_t i = 0; i < order.size(); ++i)
    CHECK(state[i + 1] == order[i]);
  CHECK(state.without(state).empty());

  cursiv::LexicalCaptures captures;
  const auto a = captures.bind(0, 9, 1, 3);
  const auto ab = captures.bind(a, 2, 4, 5);
  const auto ba = captures.bind(captures.bind(0, 2, 4, 5), 9, 1, 3);
  CHECK(ab == ba);
  CHECK(captures.bind(ab, 9, 1, 3) == ab);
  const auto changed = captures.bind(ab, 9, 6, 7);
  CHECK(captures.find(ab, 9)->begin == 1);
  CHECK(captures.find(changed, 9)->begin == 6);
  CHECK(captures.find(changed, 2)->begin == 4);
  CHECK(captures.find(0, 9) == nullptr);
}
}  // namespace

void run() {
  states();
  exercise(*tlexbackends::generated::Ll::create_parser());
  exercise(*tlexbackends::generated::Lr::create_parser());

}

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
  const auto* node = r.value_as<tthrough::Node>();
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
  check(parser, "writing:", "#[body]==:==");
  // A failed closing probe must not publish its new capture. The other
  // alternative still needs the original # mark, which survives afterwards.
  check(parser, "writing:", "#[body]==x}#:#");
  check(parser, "writing:", "#[body]==x}==:==", false);
}
}  // namespace
void run() {
  exercise(*tlexbackends::generated::Ll::create_parser());
  exercise(*tlexbackends::generated::Lr::create_parser());

}

}  // namespace lexical_through_cases

namespace rule_through_cases {

namespace {

const trulethrough::Node* read(const cursiv::ParseResult& r) {
  const bool clean =
      r.accepted && !r.cst().recovered() && r.diagnostics.empty();
  return clean ? r.value_as<trulethrough::Node>() : nullptr;
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

void run() {
  exercise(*tlexbackends::generated::Ll::create_parser());
  exercise(*tlexbackends::generated::Lr::create_parser());

}

}  // namespace rule_through_cases

int main() {
  cursiv::test::count_expectations();
  lexical_backends_cases::run();
  lexical_captures_cases::run();
  lexical_through_cases::run();
  rule_through_cases::run();
  return cursiv::test::report("LexicalBackends_test");
}
