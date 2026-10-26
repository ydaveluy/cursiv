#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "ConformanceTest.hpp"
#include "conformancell/generated/Ll/Module.hpp"
#include "conformancelr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/ConditionContractGrammar.hpp"

namespace condition_contract_cases {

// What a condition in a rule tests, in both engines: the text of the token the
// lexer chooses among the terminals the parser can take where it stands; the
// first terminals of the conditions there lengthen that token and rank
// nothing.

namespace {

void reads(const char* engine, const cursiv::Parser& p, std::string_view text,
           bool want) {
  const auto r = p.parse(text);
  const bool got = r.accepted && r.diagnostics.empty() && r.value != nullptr;
  if (got != want) {
    std::printf("%s [%.*s]: wanted %s (accepted=%d diags=%zu value=%d)\n",
                engine, static_cast<int>(text.size()), text.data(),
                want ? "read" : "refused", int(r.accepted),
                r.diagnostics.size(), int(r.value != nullptr));
    for (const auto& d : r.diagnostics)
      std::printf("    kind=%d [%u,%u) %s\n", int(d.kind), d.begin, d.end,
                  d.message.c_str());
  }
  CHECK(got == want);
}

void reads_as(const char* engine, const cursiv::Parser& p,
              std::string_view text, std::string_view name) {
  const auto r = p.parse(text);
  const auto* holder = r.value_as<tcondcontract::Holder>();
  const bool got = r.accepted && r.diagnostics.empty() && holder != nullptr &&
                   holder->name == name;
  if (!got)
    std::printf(
        "%s [%.*s]: wanted name [%.*s] (accepted=%d diags=%zu "
        "name=[%.*s])\n",
        engine, static_cast<int>(text.size()), text.data(),
        static_cast<int>(name.size()), name.data(), int(r.accepted),
        r.diagnostics.size(), holder ? int(holder->name.size()) : 0,
        holder ? holder->name.data() : "");
  CHECK(got);
}

}  // namespace

void run() {
  const auto& ll = conformance_test::engines()[0];
  const auto& lr = conformance_test::engines()[1];
  const std::pair<const char*, const cursiv::Parser*> engines[] = {
      {"LL", ll.get()}, {"LR", lr.get()}};
  for (const auto& [name, parser] : engines) {
    const auto& p = *parser;
    reads(name, p, "spec1 ab", false);
    reads(name, p, "spec1 a b", true);
    reads(name, p, "spec2 AS", false);
    reads(name, p, "spec2 ASX", true);
    reads(name, p, "spec3 let", false);
    reads(name, p, "spec3 letter", true);
    reads(name, p, "spec4 <<", true);
    reads(name, p, "spec4 <", false);
    reads(name, p, "tokand <<", false);
    reads(name, p, "tokandnot <<", true);
    reads(name, p, "tokandnot <", false);
    reads(name, p, "tokphrase <<", false);
    reads(name, p, "tokwhole <<", true);
    reads(name, p, "tokchoice <<", true);
    reads(name, p, "tokchoice x", false);
    reads(name, p, "joined ==", false);
    reads(name, p, "joined = =", true);
    reads(name, p, "head abc", false);
    reads_as(name, p, "head a", "N:a");
    reads(name, p, "head bc", false);
    reads(name, p, "inside abc", true);
    reads(name, p, "inside a", true);
    reads(name, p, "inside bc", false);
    reads_as(name, p, "same abc", "N:abc");
    reads(name, p, "same aBc", false);
    reads(name, p, "other abc", false);
    reads_as(name, p, "other aBc", "N:aBc");
    reads(name, p, "word if", true);
    reads(name, p, "notword if", false);
    reads(name, p, "litword if", true);
    reads(name, p, "litnotword if", false);
    reads(name, p, "notif if", false);
    reads_as(name, p, "notif iff", "I:iff");
    reads_as(name, p, "argv ARGV", "I:ARGV");
    reads(name, p, "argv argv", false);
    reads(name, p, "first a bc", false);
    reads(name, p, "phrase a bc", true);
    reads(name, p, "made ab", false);
    reads(name, p, "made a b", true);
    reads_as(name, p, "blank a", "I:a");
    reads(name, p, "closea }", true);
    reads(name, p, "closeb }", true);
    reads(name, p, "opener (", false);
    reads(name, p, "nest x if(", true);
    reads(name, p, "nest x if:", false);
    reads(name, p, "nest x y:", true);
    reads_as(name, p, "shared if", "I:if");
    reads_as(name, p, "shared iffy", "I:iffy");
    reads_as(name, p, "shared x", "I:x");
    reads(name, p, "sharedkw if", false);
    reads_as(name, p, "sharedkw if(", "");
    reads_as(name, p, "sharedkw iffy", "I:iffy");
    reads(name, p, "real ifx", false);
    reads(name, p, "real if x", true);
    reads(name, p, "absorb:x y", false);
    reads(name, p, "absorbnot:x y", true);
    reads(name, p, "absorbnot:x", false);
    reads(name, p, "eat:b()", true);
    reads(name, p, "eatkw:b()", true);
    reads(name, p, "eatnot:a()", true);
    reads(name, p, "eatnot:b()", false);
    reads(name, p, "commit <<", true);
    reads(name, p, "commit < x <<", true);
    reads(name, p, "run x:y end", true);
    reads(name, p, "run end", true);
    reads(name, p, "run x:y z:w end", true);
    reads(name, p, "stopc xab", false);
    reads(name, p, "stope xab", false);
    reads(name, p, "pair a b", true);
    reads(name, p, "pair ab", true);
    reads(name, p, "pair a c", false);
    reads(name, p, "paren (", true);
    reads(name, p, "sepend x:1,end", false);
    reads(name, p, "sepend x:1 , end", false);
    reads(name, p, "sepend ,end", true);
    reads(name, p, "seplist x:1", true);
    reads(name, p, "seplist x:1,y:2", true);
    reads(name, p, "seplist x:1,end", false);
    reads(name, p, "expand <<", true);
    reads(name, p, "expand < x <<", true);
  }
  for (const auto& [name, parser] : engines) {
    const auto r = parser->parse("abc");
    const auto* holder = r.value_as<tcondcontract::Holder>();
    const bool got = r.accepted && r.diagnostics.empty() && holder != nullptr &&
                     holder->name == "abc";
    for (const auto& d : r.diagnostics)
      std::printf("kind=%d [%u,%u) %s\n", int(d.kind), d.begin, d.end,
                  d.message.c_str());
    if (!got)
      std::printf("%s [own abc]: accepted=%d diags=%zu name=[%.*s]\n", name,
                  int(r.accepted), r.diagnostics.size(),
                  holder ? int(holder->name.size()) : 0,
                  holder ? holder->name.data() : "");
    CHECK(got);
  }
  for (const auto& [name, parser] : engines) {
    reads(name, *parser, "classed:{a}", true);
    reads(name, *parser, "classed:{a b}", false);
    reads(name, *parser, "classed:a", true);
  }
  for (const auto& [name, parser] : engines) {
    reads(name, *parser, "spelled:{a}", true);
    reads(name, *parser, "spelled:{a b}", false);
    reads(name, *parser, "spelled:a", true);
  }
  for (const auto& [name, parser] : engines) {
    reads(name, *parser, "@ REM hi\nx", false);
    reads(name, *parser, "@ REM\nx", false);
    reads(name, *parser, "% REM hi\nx", true);
    reads(name, *parser, "% REM\nx", true);
  }
  for (const auto& [name, parser] : engines) {
    reads(name, *parser, "tie:if", true);
    reads(name, *parser, "tie:if ;", true);
  }
}

}  // namespace condition_contract_cases

namespace template_tail_cases {

// A template tail is lexed only where a template continuation can stand, in
// both engines.

namespace {

void reads(const char* engine, const cursiv::Parser& p, std::string_view text,
           bool want) {
  const auto r = p.parse(text);
  const bool got = r.accepted && r.diagnostics.empty() && r.value != nullptr;
  if (got != want)
    std::printf("%s [%.*s]: wanted %s\n", engine, static_cast<int>(text.size()),
                text.data(), want ? "read" : "refused");
  CHECK(got == want);
}

}  // namespace

void run() {
  const auto& ll = conformance_test::engines()[0];
  const auto& lr = conformance_test::engines()[1];
  const std::pair<const char*, const cursiv::Parser*> engines[] = {
      {"LL", ll.get()}, {"LR", lr.get()}};
  for (const auto& [name, parser] : engines) {
    const auto& p = *parser;
    reads(name, p, "function f { x = 1 }", true);
    reads(name, p, "function f { x = `a${1}b` }", true);
    reads(name, p, "function f { x = 1 }\nfunction g { x = `a${1}b` }", true);
    reads(name, p,
          "function f { x = 1 }\nfunction g { y = 2; x = `a${z = 1}b` }", true);
    reads(name, p, "function f { x = `a${1}b`; y = 2 }\nfunction g { x = 1 }",
          true);
    reads(name, p, "function f { x = 1 }\nfunction g { x = `a${1` }", false);
  }
}

}  // namespace template_tail_cases
