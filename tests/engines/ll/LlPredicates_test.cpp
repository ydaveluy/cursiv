#include <array>
#include <initializer_list>
#include <string>
#include <string_view>

#include "cursiv/test/Check.hpp"
#include "engines/ll/PredicatesGrammar.hpp"
#include "predicatesll/generated/Ll/Module.hpp"

namespace {

bool clean(const cursiv::Parser& parser, std::string_view text) {
  const auto result = parser.parse(text);
  return result.accepted && result.diagnostics.empty() &&
         result.value != nullptr;
}

void same(const cursiv::Parser& parser, std::string_view predicate,
          std::string_view ordinary, std::string_view suffix) {
  const bool a = clean(parser, std::string(predicate) + std::string(suffix));
  const bool b = clean(parser, std::string(ordinary) + std::string(suffix));
  if (a != b)
    std::printf(
        "recognition mismatch %.*s / %.*s input=%.*s predicate=%d normal=%d\n",
        int(predicate.size()), predicate.data(), int(ordinary.size()),
        ordinary.data(), int(suffix.size()), suffix.data(), a, b);
  CHECK(a == b);
}

void ranks(const cursiv::Parser& parser, std::string_view predicate,
           std::string_view ordinary, std::string_view suffix) {
  const bool a = clean(parser, std::string(predicate) + std::string(suffix));
  const bool b = clean(parser, std::string(ordinary) + std::string(suffix));
  if (!a || b)
    std::printf(
        "ranking mismatch %.*s / %.*s input=%.*s predicate=%d normal=%d\n",
        int(predicate.size()), predicate.data(), int(ordinary.size()),
        ordinary.data(), int(suffix.size()), suffix.data(), a, b);
  CHECK(a && !b);
}

bool among(std::string_view suffix,
           std::initializer_list<std::string_view> list) {
  for (const std::string_view item : list)
    if (item == suffix) return true;
  return false;
}

void normal_recognition_is_preserved(const cursiv::Parser& parser) {
  for (const std::string_view suffix :
       {"ab", "abc", "a b", "a/*c*/b", "a\nb"}) {
    const bool normal = clean(parser, std::string("r:") + std::string(suffix));
    CHECK(clean(parser, std::string("p:") + std::string(suffix)) == normal);
    CHECK(clean(parser, std::string("n:") + std::string(suffix)) != normal);
  }
  // A condition ranks nothing: it reads with the pattern a word the rule's
  // literal takes from it.
  for (const auto prefixes :
       {std::array<std::string_view, 2>{"choice:", "choice-read:"},
        {"optional:", "optional-read:"},
        {"many:", "many-read:"}})
    for (const std::string_view suffix :
         {"b", "ab", "a b", "a a b", "b b", "b a b", "a/*x*/b", "abc", " ab",
          "/*c*/a b", "\nb b"}) {
      const bool ranked =
          prefixes[0] == "optional:" ? among(suffix, {"b b", "\nb b"})
          : prefixes[0] == "many:"   ? among(suffix, {"b b", "b a b", "\nb b"})
                                     : false;
      if (ranked)
        ranks(parser, prefixes[0], prefixes[1], suffix);
      else
        same(parser, prefixes[0], prefixes[1], suffix);
    }
  CHECK(!clean(parser, "choice-read:ab"));
  CHECK(!clean(parser, "choice:ab"));
  for (const std::string_view suffix :
       {"a", "a x a", "a a", "x", " a", "/*c*/a "})
    same(parser, "commit:", "commit-read:", suffix);
  // A committed repetition stops only where its prefix cannot be read, trivia
  // between the rounds or not.
  for (const std::string_view suffix :
       {"x", "xa", "xab", "xabab", "xaba", "xabx", "xabc", "xc"})
    same(parser, "tail-c:", "tight-tail-c:", suffix);
  for (const std::string_view suffix :
       {"a a{", "a{", "a a a{", "a/*x*/a{", "/*c*/a x{"})
    CHECK(clean(parser, std::string("exact:") + std::string(suffix)) ==
          clean(parser, std::string("exact-read:") + std::string(suffix)));
  for (const std::string_view suffix : {"if", "ifx", "name"})
    CHECK(clean(parser, std::string("guard:") + std::string(suffix)) ==
          clean(parser, std::string("guard-read:") + std::string(suffix)));
  for (const std::string_view suffix : {"12.", "12", "1 2."})
    CHECK(clean(parser, std::string("inline:") + std::string(suffix)) ==
          clean(parser, std::string("inline-read:") + std::string(suffix)));
  for (const auto kind :
       {"many", "some", "exact", "bounded", "many-c", "some-c", "bounded-c"}) {
    const auto prefix = std::string("region-") + kind;
    for (const auto suffix :
         {"b", "ab", "aab", "aaab", "aaaab", "a ab", "a aab", "a/*x*/ab",
          "a\nab", "aa b", "a a b", "aa/*x*/b", "a a/*x*/b", " ab", " aab",
          "/*x*/a ab"})
      same(parser, prefix + ":", prefix + "-read:", suffix);
  }
  for (const auto suffix : {"aab", "a ab", "a/*x*/ab"})
    CHECK(clean(parser, std::string("region-exact:") + suffix));
  for (const auto suffix : {"aa b", "a a b", "aa/*x*/b"})
    CHECK(!clean(parser, std::string("region-exact:") + suffix));
  for (const auto kind : {"choice", "option", "many"}) {
    const auto prefix = std::string("keyword-") + kind;
    for (const auto suffix :
         {"if", "if x", "if y", "ifx y", "else y", "if if", "ifx if", "name if",
          "name name if", "if/*x*/y"}) {
      const bool ranked = std::string_view(kind) == "choice"
                              ? among(suffix, {"if y", "if/*x*/y"})
                              : std::string_view(suffix) == "if if";
      if (ranked)
        ranks(parser, prefix + ":", prefix + "-read:", suffix);
      else
        same(parser, prefix + ":", prefix + "-read:", suffix);
    }
  }
  CHECK(clean(parser, "keyword-choice:if y"));
  CHECK(!clean(parser, "keyword-choice-read:if y"));
  CHECK(clean(parser, "keyword-choice:if x"));
  CHECK(clean(parser, "keyword-choice:ifx y"));
  for (const auto suffix : {"if x", "if y", "if/*x*/y"}) {
    same(parser, "equal-priority:", "equal-priority-read:", suffix);
    CHECK(clean(parser, std::string("equal-priority:") + suffix));
  }
  for (const auto suffix : {"if x", "if y", "ifx y", "raw x", "raw y", "rawx y",
                            "raw/*c*/x", "raw/*c*/y"}) {
    if (among(suffix, {"if y", "raw y", "raw/*c*/y"}))
      ranks(parser, "mixed-choice:", "mixed-choice-read:", suffix);
    else
      same(parser, "mixed-choice:", "mixed-choice-read:", suffix);
  }
  CHECK(clean(parser, "mixed-choice:raw x"));
  CHECK(clean(parser, "mixed-choice:raw y"));
  CHECK(!clean(parser, "mixed-choice-read:raw y"));
  CHECK(clean(parser, "mixed-choice:if y"));
  for (const auto suffix : {"ifx", "ify", "if x", "if y", "if/*c*/x",
                            "if/*c*/y", "aby", "ab y", "abc y"}) {
    if (among(suffix, {"ify", "if y", "if/*c*/y"}))
      ranks(parser, "mixed-boundary:", "mixed-boundary-read:", suffix);
    else
      same(parser, "mixed-boundary:", "mixed-boundary-read:", suffix);
  }
  // The keyword branch ranks the terminal as a keyword where `if` ends a word,
  // and the literal branch as a literal where a word byte follows: the pattern
  // cannot read `if` in either place, and a condition reads it there all the
  // same.
  CHECK(clean(parser, "mixed-boundary:ifx"));
  CHECK(clean(parser, "mixed-boundary:ify"));
  CHECK(clean(parser, "mixed-boundary:if y"));
  CHECK(!clean(parser, "mixed-boundary-read:if y"));
  for (const auto suffix :
       {"a", "aa", "aaa", "aaaa", "aaaaa", "aaaaaa", "aaaaaaa", "aaaaaaaa",
        "aaaaaaaaa", "aaaaaaaaaa", "a a a a a", "aa aa a", "a/*x*/a a a a"})
    same(parser, "counted-c:", "counted-c-read:", suffix);
  CHECK(clean(parser, "counted-c:aaaaa"));
  CHECK(!clean(parser, "counted-c:aaaa"));
}

void predicates_compose_without_consuming(const cursiv::Parser& parser) {
  for (const std::string_view suffix :
       {"x{", "xy{", "name {", "name/*comment*/{"}) {
    const std::string text = std::string("@") + std::string(suffix);
    const auto result = parser.parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
    CHECK(result.value_as<tpredicates::Node>() != nullptr);
    if (const auto* node = result.value_as<tpredicates::Node>())
      CHECK(node->text == suffix.substr(0, suffix.find_first_of(" {/*")));
    CHECK(clean(parser, std::string(":") + std::string(suffix)));
  }
  CHECK(clean(parser, ":a.b{"));
  CHECK(!clean(parser, "@a.b{"));
  CHECK(clean(parser, "prefix-negation:abc"));
  CHECK(clean(parser, "prefix-negation:ac"));
  CHECK(!clean(parser, "prefix-negation:abx"));
  CHECK(!clean(parser, "prefix-negation:ax"));
  for (const std::string_view text : {"and:abc",
                                      "notnot:abc",
                                      "not:123",
                                      "andnot:123",
                                      "seq:name{",
                                      "seq:name /*c*/ {",
                                      "nested:name{}",
                                      "nested:name /*c*/ { }",
                                      "gap:name{",
                                      "tight:name{",
                                      "end:name",
                                      "nested:123",
                                      "composite:abc",
                                      "composite:ifx",
                                      "either:if",
                                      "either:123",
                                      "white: abc",
                                      "white:\nabc",
                                      "raw:/*  x */abc",
                                      "unicode-gap:/*xβ*/abc",
                                      "unicode-inner:abc/*xβ*/{"})
    if (!clean(parser, text)) {
      std::printf("expected success: %.*s\n", int(text.size()), text.data());
      CHECK(false);
    }
  for (const std::string_view text :
       {"head:abc", "head: abc", "head:/*c*/abc", "tight-head:abc",
        "tight-head: abc", "tight-head:/*c*/abc"})
    if (!clean(parser, text)) {
      std::printf("expected success: %.*s\n", int(text.size()), text.data());
      CHECK(false);
    }
  for (const std::string_view text : {"and:123",
                                      "notnot:123",
                                      "not:abc",
                                      "andnot:abc",
                                      "seq:name",
                                      "nested:name{",
                                      "nested:name",
                                      "gap:name {",
                                      "gap:name/*c*/{",
                                      "tight:name {",
                                      "tight:name/*c*/{",
                                      "end:name other",
                                      "composite:if",
                                      "composite: abc",
                                      "composite:/*c*/abc",
                                      "either:abc",
                                      "white:abc",
                                      "white:/* */abc",
                                      "raw:/* x */abc",
                                      "unicode-gap:/*xα*/abc",
                                      "unicode-inner:abc/*xα*/{"})
    if (clean(parser, text)) {
      std::printf("expected refusal: %.*s\n", int(text.size()), text.data());
      CHECK(false);
    }
  // A token longer than the condition's is the one in hand: the condition
  // reads all of it, or it fails.
  CHECK(clean(parser, "absorb-id:ab"));
  CHECK(!clean(parser, "absorb-id:ab c"));
  CHECK(clean(parser, "absorb-not:ab c"));
  CHECK(!clean(parser, "absorb-not:ab"));
  CHECK(clean(parser, "absorb-lit:ab"));
  CHECK(!clean(parser, "absorb-lit:a"));
}

}  // namespace

int main() {
  for (const auto& parser : {tpredicates::generated::Ll::create_parser()}) {
    normal_recognition_is_preserved(*parser);
    predicates_compose_without_consuming(*parser);
  }
  return cursiv::test::report("LlPredicates_test");
}
