#include <string>
#include <string_view>

#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/LexicalPredicatesGrammar.hpp"
#include "lexicalpredicatesll/generated/Ll/Module.hpp"
#include "lexicalpredicatesll/generated/Ll/Table.hpp"

namespace {

bool clean(const cursiv::Parser& parser, std::string_view text) {
  const auto result = parser.parse(text);
  return result.accepted && result.diagnostics.empty() &&
         result.value != nullptr;
}

void check(const cursiv::Parser& parser, std::string_view text, bool expected) {
  const bool actual = clean(parser, text);
  if (actual != expected)
    std::printf("lexical predicate input=%.*s expected=%d actual=%d\n",
                int(text.size()), text.data(), expected, actual);
  CHECK(actual == expected);
}

void exercise(const cursiv::Parser& parser) {
  for (const auto tail : {"#a", "#b", "#bc", "#bcd", "#bx", "#aaaa"})
    CHECK(clean(parser, std::string("s:") + tail) ==
          clean(parser, std::string("c:") + tail));
  for (const auto prefix : {"n:", "g:", "pg:", "ng:"}) {
    const std::string good = std::string(prefix) + "abc";
    check(parser, good, true);
    const auto parsed = parser.parse(good);
    const auto* node = parsed.value_as<tlexicalpredicates::Node>();
    CHECK(node && node->text == "ab");
    for (const auto tail : {"ab", "abd", "a bc", "ab c", "ab/*x*/c"})
      check(parser, std::string(prefix) + tail, false);
  }
  for (const auto text : {"p:ab",
                          "double:ab",
                          "negative:ab",
                          "end:a",
                          "notend:ab",
                          "loose:abc",
                          "loose:ax",
                          "final:|x",
                          "final:| x",
                          "name:aname{",
                          "nameguard:aname{",
                          "string:ax",
                          "string:a'unclosed",
                          "amb:abd",
                          "repeat:abc",
                          "repeat:abbbc",
                          "nullable:abbbc",
                          "loop:a",
                          "loop:abbb",
                          "emptyloop:ab",
                          "longest:abc",
                          "longest:ab",
                          "keyword:aASX",
                          "guarded:abd",
                          "prefix:ab",
                          "unicodekw:aASα",
                          "unicode:aαx",
                          "unicode:aβx",
                          "echo:a<hi-hi>",
                          "echo:a<h-h>",
                          "echothrough:a[==[text]==]",
                          "nestedthrough:a/*x/*y*/z*/",
                          "longcloser:a<bcd",
                          "longcloser:a<bd",
                          "through:a'hello'"})
    check(parser, text, true);
  for (const auto text : {"p:a b",
                          "p:a/*x*/b",
                          "double:ac",
                          "negative:ac",
                          "end:ab",
                          "loose:abd",
                          "final:|",
                          "final:|=x",
                          "name:aname {",
                          "nameguard:aname/*x*/{",
                          "notend:a",
                          "notend:a=b",
                          "string:a'closed'",
                          "amb:abcd",
                          "repeat:abbb",
                          "repeat:ab c",
                          "nullable:ab c",
                          "loop:ab x",
                          "emptyloop:ac",
                          "keyword:aAS",
                          "guarded:abc",
                          "prefix:ac",
                          "through:a'unclosed",
                          "unicodekw:aAS",
                          "unicodekw:aAS{",
                          "unicode:aαb",
                          "unicode:a αx",
                          "echo:a<h-hi>",
                          "echothrough:a[==[text]=]",
                          "nestedthrough:a/*x/*y*/",
                          "longcloser:a<bce"})
    check(parser, text, false);
  for (const auto text : {"kwassert:aAS", "kwassert:aAS{", "kwassert:aAS-é"})
    check(parser, text, true);
  for (const auto text : {"kwassert:aASé", "kwassert:aASα", "kwassert:aASx",
                          "kwassert:aAS_", "kwassert:aAB"})
    check(parser, text, false);
}

void equal_choices(const cursiv::Parser& parser) {
  for (const auto text :
       {"equal:abc", "equal:abx", "equal:abce", "equal-not:abcd",
        "equal-not:abxd", "equal-not:abb", "empty-choice:ab"})
    check(parser, text, true);
  for (const auto text :
       {"equal:abcd", "equal:abxd", "equal:abb", "equal:ab", "equal-not:abc",
        "equal-not:abx", "empty-choice:ac", "empty-choice:a b"})
    check(parser, text, false);
}

void exact_repetitions(const cursiv::Parser& parser) {
  for (const auto text :
       {"exact:abcbc", "exact:abcbce", "exact-choice:abb", "exact-choice:abc",
        "exact-choice:acb", "exact-choice:acc", "exact-assertions:ab",
        "twin:ax", "twin:ay"})
    check(parser, text, true);
  for (const auto text :
       {"exact:abc", "exact:abcb", "exact:abcbcd", "exact:abc bc",
        "exact-choice:ab", "exact-choice:abcd", "exact-choice:ab c",
        "exact-assertions:a", "exact-assertions:a b", "twin:az"})
    check(parser, text, false);
}

void enumerate(const cursiv::Parser& parser, std::string text, unsigned left) {
  const bool nested = text.size() > 1 &&
                      (text[1] != 'b' || (text.size() > 2 && text[2] == 'c'));
  const bool ambiguous =
      text.size() > 1 &&
      (text[1] != 'b' || (text.size() > 2 && text[2] == 'd'));
  check(parser, "loose:" + text, nested);
  check(parser, "amb:" + text, ambiguous);
  if (left == 0) return;
  for (const char ch : std::string_view("bcd /"))
    enumerate(parser, text + ch, left - 1);
}

void first_position() {
  namespace ll = tlexicalpredicates::generated::Ll;
  const auto llTerm = [&] {
    for (unsigned t = 1; t < ll::kTable.nterms; ++t)
      if (std::string_view(ll::kTable.names + ll::kTable.terms[t].nameOff,
                           ll::kTable.terms[t].nameLen) == "Prefix")
        return t;
    return 0u;
  }();
  CHECK(llTerm != 0);
  bool llRead = false;
  for (unsigned m = 0; m < ll::kTable.nmodes; ++m) {
    const auto read = ::cursiv::ll_match_text<ll::kTable>("ab", 2, m, 0);
    if (read.term != llTerm) continue;
    CHECK(read.end == 2);
    CHECK(::cursiv::ll_match_text<ll::kTable>("cb", 2, m, 0).term != llTerm);
    llRead = true;
    break;
  }
  CHECK(llRead);

}

}  // namespace

int main() {
  for (const auto& parser :
       {tlexicalpredicates::generated::Ll::create_parser()}) {
    exercise(*parser);
    equal_choices(*parser);
    exact_repetitions(*parser);
    enumerate(*parser, "a", 4);
  }
  first_position();
  return cursiv::test::report("LexicalPredicates_test");
}
