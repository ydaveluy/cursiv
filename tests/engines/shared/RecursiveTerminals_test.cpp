#include "cursiv/parser/Parser.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/RecursiveTerminalsGrammar.hpp"
#include "lexicalbackendslr/generated/Suite/Module.hpp"
#include "lexicalbackendslr/generated/Suite/Table.hpp"
#include <cstdio>
#include <string>
#include <string_view>

namespace recursive_terminals_cases {

namespace {

void check(const cursiv::Parser& parser, std::string_view text, bool expected) {
  const auto result = parser.parse(text);
  const bool actual = result.accepted && result.diagnostics.empty() &&
                      result.valueAs<trecursive::Node>() != nullptr;
  if (actual != expected)
    std::printf("recursive terminal input=%.*s expected=%d actual=%d\n",
                static_cast<int>(text.size() > 80 ? 80 : text.size()),
                text.data(), expected, actual);
  CHECK(actual == expected);
}

void exercise(const cursiv::Parser& parser) {
  for (const auto text : {"directive:#![", "directive:#! [", "directive:#!/*a/*b*/c*/ ["}) {
    const auto result = parser.parse(text);
    const auto* node = result.valueAs<trecursive::Node>();
    CHECK(result.accepted && result.diagnostics.empty() && node && node->text.empty());
  }
  for (const auto text : {"comment:/**/!",
                          "directive:#!/bin/sh",
                          "comment:/* /* x */ /* y */ */!",
                          "comment:/*/*/**/*/*/!",
                          "pair:/**/x/* /* */ */",
                          "mutual:()!",
                          "mutual:([([x])])!",
                          "code:{}!",
                          "code:{\"}{\" {\"}\"}}!",
                          "code:{\"escaped \\\" }\"}!",
                          "continuation:aab!",
                          "continuation:ab!",
                          "continuation:abb!",
                          "optional:q!",
                          "optional:nnq!",
                          "asserted:{{}}!",
                          "checked:((()))!",
                          "unfollowed:((()))!",
                          "refused:b!",
                          "many:z!",
                          "many:{}{{}}z!",
                          "some:{}z!",
                          "exact:{}{}z!",
                          "through:{} body!",
                          "through:{{}}!",
                          "emptyopen:;",
                          "emptyopen:a;",
                          "emptyopen:;a;",
                          "marked:<==>{{}}==;",
                          "markclass:<=>=;",
                          "markclass:<->-;",
                          "marklit:<#>#;",
                          "unicode:λω!",
                          "unicode:λλωω!",
                          "echo:<-->body]--;",
                          "echo:<->]-;x<-->]--;",
                          "paddecho:<->]-;",
                          "paddecho:<->]---;",
                          "emptyecho:<>] ",
                          "emptyecho:<>]   ",
                          "altecho:<aa:]bbbaa;",
                          "altecho:(bb:]aaabb;",
                          "altecho:<a:]a;x(b:]ab;",
                          "guardonly:#(())",
                          "dataonly:b()",
                          "dataonly:b(())",
                          "section:<(())END",
                          "same:<<!",
                          "longclose:xyyy!",
                          "opener:ab!",
                          "multi:{|(|x|}|)!",
                          "open:<*",
                          "open:<* x",
                          "open:<* <* x *> y",
                          "open:<* <* x *> y *>",
                          "positive:@/* /* */ */",
                          "negative:~/* /* */",
                          "negative:~plain"})
    check(parser, text, true);
  for (const auto text : {"comment:/* /* x */!",
                          "comment:/* unclosed!",
                          "comment:/**/x/**/!",
                          "mutual:([x)!",
                          "mutual:((x))!",
                          "code:{\"unclosed}!",
                          "code:{{}!",
                          "continuation:aa!",
                          "asserted:{{}!",
                          "checked:(()!",
                          "unfollowed:(())#!",
                          "asserted:x!",
                          "asserted:",
                          "refused:{}!",
                          "some:z!",
                          "exact:{}z!",
                          "exact:{}{}{}z!",
                          "through:{}",
                          "emptyopen:",
                          "emptyopen:x",
                          "marked:<==>{{}}=;",
                          "markclass:<=>-;",
                          "markclass:<==>==;",
                          "marklit:<#>=;",
                          "unicode:x!",
                          "unicode:δω!",
                          "echo:<-->body]-;",
                          "paddecho:<->];",
                          "emptyecho:<>]",
                          "altecho:<aa:]bbba;",
                          "altecho:(bb:]aaab;",
                          "unicode:",
                          "guardonly:#(()",
                          "dataonly:b(x)",
                          "dataonly:b(()",
                          "section:<(())EN",
                          "same:<<<<!",
                          "shortclose:xyyy!",
                          "opener:aabb!",
                          "multi:{|(|x|}!",
                          "positive:@/* /* */",
                          "negative:~/**/"})
    check(parser, text, false);

  const auto pair = parser.parse("pair:/**/x/* /* */ */");
  const auto* node = pair.valueAs<trecursive::Node>();
  CHECK(node != nullptr);
  if (node != nullptr) {
    CHECK(node->parts.size() == 2);
    if (node->parts.size() == 2) {
      CHECK(node->parts[0] == "/**/");
      CHECK(node->parts[1] == "/* /* */ */");
    }
  }

  // A nested comment the text ends inside is read as one up to the end.
  {
    const std::string_view text = "comment:/* /* x */ y";
    const auto r = parser.parse(text);
    const auto* read = r.valueAs<trecursive::Node>();
    CHECK(r.accepted && read != nullptr && read->text == text.substr(text.find("/*")));
    CHECK(!r.diagnostics.empty() && r.diagnostics[0].begin == text.find("/*"));
  }

  std::string deep = "comment:";
  for (unsigned i = 0; i < 100000; ++i) deep += "/*";
  for (unsigned i = 0; i < 100000; ++i) deep += "*/";
  deep += '!';
  check(parser, deep, true);

  std::string generic = "unicode:";
  for (unsigned i = 0; i < 2000; ++i) generic += "λ";
  for (unsigned i = 0; i < 2000; ++i) generic += "ω";
  generic += '!';
  check(parser, generic, true);

  const std::string mark(2048, '-');
  check(parser, "echo:<" + mark + ">" + mark + "!", false);
  check(parser, "echo:<" + mark + ">]" + mark + ";", true);
  // The searches of a token share what they read: a group nested 20000 deep
  // that checks each level before reading it reads in linear time.
  for (const auto prefix : {"checked:", "unfollowed:"})
    check(parser, prefix + std::string(20000, '(') + std::string(20000, ')') + "!", true);
}

void check_window(std::string_view prefix, std::string_view text, unsigned width,
                  bool recognized, bool atCut) {
  using namespace cursiv::engine;
  const auto& t = tlexbackends::generated::Suite::kTables;
  const std::string source = std::string(prefix) + std::string(text);
  cursiv::Cst cst(source);
  cursiv::CstBuilder builder(cst);
  Machine m(t, source, builder.stream(), cursiv::kNoCancel);
  const char* begin = m.src + prefix.size();
  m.read_to(begin, t.terminalCount);
  CHECK(t.drive(m) == Exit::refused);
  m.read_to(begin + width, t.terminalCount);
  const auto answer = m.on_text([&] {
    return m.walk(begin, t.stateMode[m.under()], 1, begin).terminal() != t.terminalCount;
  });
  CHECK(answer.value == recognized && answer.atCut == atCut);
}

}  // namespace

void a_stop_reaches_a_long_token(const cursiv::Parser& parser) {
  using namespace cursiv::engine;
  const auto& t = tlexbackends::generated::Suite::kTables;
  const std::string text = "continuation:" + std::string(8000, 'a') + std::string(8000, 'b') + "!";
  check(parser, text, true);
  cursiv::CancellationTokenSource stop;
  const auto token = stop.get_token();
  {
    cursiv::Cst cst(text);
    cursiv::CstBuilder builder(cst);
    Machine m(t, text, builder.stream(), token);
    const char* const begin = m.src + std::string_view("continuation:").size();
    m.read_to(begin, t.terminalCount);
    CHECK(t.drive(m) == Exit::refused);
    m.read_to(m.end, t.terminalCount);
    // Requested before the token is read: its reading stops.
    stop.request_stop();
    bool cancelled = false;
    try {
      (void)m.walk<true>(begin, t.stateMode[m.under()], 1, begin);
    } catch (const cursiv::OperationCancelled&) {
      cancelled = true;
    }
    CHECK(cancelled);
  }
  cursiv::Cst cst;
  try {
    parse(t, text, cst, token);
    CHECK(false);
  } catch (const cursiv::OperationCancelled&) {
    CHECK(cst.text().empty());
  }
}

void run() {
  const auto parser = tlexbackends::generated::Suite::createParser();
  exercise(*parser);
  check_window("comment:", "/* a */!", 5, false, true);
  check_window("comment:", "/* a */!", 8, true, false);
  check_window("scan:one:", "D1xD1y", 2, false, true);
  check_window("scan:one:", "D1xD1y", 4, true, true);
  a_stop_reaches_a_long_token(*parser);
  check(*parser, "(())", true);
  check(*parser, "(", false);
}

}  // namespace recursive_terminals_cases

namespace lex_right_linear_cases {

namespace {
void check(const cursiv::Parser& parser, std::string_view prefix,
           std::string_view body, bool expected = true) {
  const std::string source =
      "rightlinear:" + std::string(prefix) + std::string(body);
  const auto r = parser.parse(source);
  const auto* node = r.valueAs<trightlinear::Node>();
  const bool clean =
      r.accepted && !r.cst().recovered() && r.diagnostics.empty() && node;
  if (clean != expected)
    std::fprintf(stderr, "right-linear %.*s (%zu bytes): expected %d, got %d\n",
                 static_cast<int>(prefix.size()), prefix.data(), body.size(),
                 expected, clean);
  CHECK(clean == expected);
  if (clean && expected) CHECK(node->text == body);
}

void exercise(const cursiv::Parser& parser) {
  for (unsigned n = 1; n <= 12; ++n) {
    check(parser, "run:", std::string(n, 'a'));
    check(parser, "alternative:", std::string(n, 'c'));
    check(parser, "reversed:", std::string(n, 'd'));
    check(parser, "counted:", std::string(n, 'e'));
    check(parser, "suffix:", std::string(n + 1, 'a') + "!");
    check(parser, "wrapped:", "#" + std::string(n, 'a') + "#");
    std::string pairs, unicode;
    for (unsigned k = 0; k < n; ++k) {
      pairs += "ab";
      unicode += "λ😀";
    }
    check(parser, "sequence:", pairs);
    check(parser, "seqalt:", pairs);
    check(parser, "unicode:", unicode);
  }
  check(parser, "bigcount:", std::string(100000, 'z'));
  check(parser, "ping:", "i");
  check(parser, "ping:", "o");
  check(parser, "ping:", "io", false);
  for (auto text : {"a", "ab", "aab", "aba", "abab", "abaab"})
    check(parser, "ambiguous:", text);
  for (auto text : {"ab!", "abb!", "aabb!", "abab!"})
    check(parser, "ambsuffix:", text);
  check(parser, "variable:", "xyxxxyxxy");
  check(parser, "marked:", "mmm");
  check(parser, "marked:", "msm");
  check(parser, "marked:", "mmsm");
  check(parser, "marked:", "mss", false);
  check(parser, "guarded:", "ggg");
  check(parser, "guarded:", "gx", false);
  check(parser, "end:", "q");
  check(parser, "end:", "qq", false);
  check(parser, "nested:", "((()))");
  check(parser, "nested:", "((())", false);
  for (auto prefix :
       {"run:", "sequence:", "ambiguous:", "alternative:", "reversed:",
        "seqalt:", "unicode:", "counted:", "variable:"})
    check(parser, prefix, "", false);
  check(parser, "sequence:", "aba", false);
  check(parser, "suffix:", "a!", false);
  check(parser, "wrapped:", "#aaa@", false);

  check(parser, "run:", std::string(100000, 'a'));
  check(parser, "alternative:", std::string(100000, 'c'));
  std::string pairs;
  for (unsigned i = 0; i < 50000; ++i) pairs += "ab";
  check(parser, "sequence:", pairs);
}
}  // namespace

void run() { exercise(*tlexbackends::generated::Suite::createParser()); }

}  // namespace lex_right_linear_cases

int main() {
  cursiv::test::countExpectations();
  recursive_terminals_cases::run();
  lex_right_linear_cases::run();
  return cursiv::test::report("RecursiveTerminals_test");
}
