#include <cstdio>
#include <string>
#include <string_view>

#include "ConformanceTest.hpp"
#include "CaretOffers.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/ConditionContractGrammar.hpp"

namespace condition_contract_cases {

// What a condition in a rule tests: the token the lexer chooses among the
// terminals the parser can take there.

namespace {

void reads(const cursiv::Parser& p, std::string_view text, bool want) {
  const auto r = p.parse(text);
  const bool got = r.accepted && r.diagnostics.empty() && r.value != nullptr;
  if (got != want) {
    std::printf("[%.*s]: wanted %s (accepted=%d diags=%zu value=%d)\n",
                static_cast<int>(text.size()), text.data(),
                want ? "read" : "refused", int(r.accepted),
                r.diagnostics.size(), int(r.value != nullptr));
    for (const auto& d : r.diagnostics)
      std::printf("    kind=%d [%u,%u) %s\n", int(d.kind), d.begin, d.end,
                  d.message.c_str());
  }
  CHECK(got == want);
}

void reads_as(const cursiv::Parser& p, std::string_view text,
              std::string_view name) {
  const auto r = p.parse(text);
  const auto* holder = r.valueAs<tcondcontract::Holder>();
  const bool got = r.accepted && r.diagnostics.empty() && holder != nullptr &&
                   holder->name == name;
  if (!got)
    std::printf(
        "[%.*s]: wanted name [%.*s] (accepted=%d diags=%zu "
        "name=[%.*s])\n",
        static_cast<int>(text.size()), text.data(),
        static_cast<int>(name.size()), name.data(), int(r.accepted),
        r.diagnostics.size(), holder ? int(holder->name.size()) : 0,
        holder ? holder->name.data() : "");
  CHECK(got);
}

}  // namespace

void run() {
  const cursiv::Parser& p = conformance_test::parser();
  reads(p, "nullableprefix x", true);
  reads(p, "nullableprefix ~~{}x", true);
  reads(p, "nullableprefix ~~{x", false);
  reads(p, "spec1 ab", false);
  reads(p, "spec1 a b", true);
  reads(p, "spec2 AS", false);
  reads(p, "spec2 ASX", true);
  reads(p, "spec3 let", false);
  reads(p, "spec3 letter", true);
  reads(p, "spec4 <<", true);
  reads(p, "spec4 <", false);
  reads(p, "tokand <<", false);
  reads(p, "tokandnot <<", true);
  reads(p, "tokandnot <", false);
  reads(p, "tokphrase <<", false);
  reads(p, "tokwhole <<", true);
  reads(p, "tokchoice <<", true);
  reads(p, "tokchoice x", false);
  reads(p, "condskip x_y", true);
  reads(p, "condskip xy", true);
  reads(p, "condskip x y", false);
  reads(p, "joined ==", false);
  reads(p, "joined = =", true);
  reads(p, "head abc", false);
  reads_as(p, "head a", "N:a");
  reads(p, "head bc", false);
  reads(p, "inside abc", true);
  reads(p, "inside a", true);
  reads(p, "inside bc", false);
  reads_as(p, "same abc", "N:abc");
  reads(p, "same aBc", false);
  reads(p, "other abc", false);
  reads_as(p, "other aBc", "N:aBc");
  reads(p, "word if", true);
  reads(p, "notword if", false);
  reads(p, "litword if", true);
  reads(p, "litnotword if", false);
  reads(p, "notif if", false);
  reads_as(p, "notif iff", "I:iff");
  const auto guarded = caret_offers::offered(p, "argv ", 5);
  CHECK(std::ranges::find(guarded, "ID") != guarded.end());
  reads_as(p, "argv ARGV", "I:ARGV");
  reads(p, "argv argv", false);
  reads(p, "first a bc", false);
  reads(p, "phrase a bc", true);
  reads(p, "made ab", false);
  reads(p, "made a b", true);
  reads_as(p, "blank a", "I:a");
  reads(p, "closea }", true);
  reads(p, "closeb }", true);
  reads(p, "opener (", false);
  reads(p, "nest x if(", true);
  reads(p, "nest x if:", false);
  reads(p, "nest x y:", true);
  reads_as(p, "shared if", "I:if");
  reads_as(p, "shared iffy", "I:iffy");
  reads_as(p, "shared x", "I:x");
  reads(p, "sharedkw if", false);
  reads_as(p, "sharedkw if(", "");
  reads_as(p, "sharedkw iffy", "I:iffy");
  reads(p, "real ifx", false);
  reads(p, "real if x", true);
  reads(p, "absorb:x y", false);
  reads(p, "absorbnot:x y", true);
  reads(p, "absorbnot:x", false);
  reads(p, "eat:b()", true);
  reads(p, "eatkw:b()", true);
  reads(p, "eatnot:a()", true);
  reads(p, "eatnot:b()", false);
  reads(p, "commit <<", true);
  reads(p, "commit < x <<", true);
  reads(p, "run x:y end", true);
  reads(p, "run end", true);
  reads(p, "run x:y z:w end", true);
  reads(p, "stopc xab", false);
  reads(p, "stope xab", false);
  reads(p, "pair a b", true);
  reads(p, "pair ab", true);
  reads(p, "pair a c", false);
  reads(p, "paren (", true);
  reads(p, "sepend x:1,end", false);
  reads(p, "sepend x:1 , end", false);
  reads(p, "sepend ,end", true);
  reads(p, "seplist x:1", true);
  reads(p, "seplist x:1,y:2", true);
  reads(p, "seplist x:1,end", false);
  reads(p, "expand <<", true);
  reads(p, "expand < x <<", true);
  // A condition at the head of an operand lengthens the token for a sibling
  // arm, however the operand is called.
  reads(p, "direct - x", true);
  reads(p, "direct - >", true);
  reads(p, "direct ->", false);
  reads(p, "climbed - x + y", true);
  reads(p, "climbed - >", true);
  reads(p, "climbed ->", false);
  const auto r = p.parse("abc");
  const auto* holder = r.valueAs<tcondcontract::Holder>();
  const bool got = r.accepted && r.diagnostics.empty() && holder != nullptr &&
                   holder->name == "abc";
  for (const auto& d : r.diagnostics)
    std::printf("kind=%d [%u,%u) %s\n", int(d.kind), d.begin, d.end,
                d.message.c_str());
  if (!got)
    std::printf("[own abc]: accepted=%d diags=%zu name=[%.*s]\n",
                int(r.accepted), r.diagnostics.size(),
                holder ? int(holder->name.size()) : 0,
                holder ? holder->name.data() : "");
  CHECK(got);
  reads(p, "classed:{a}", true);
  reads(p, "classed:{a b}", false);
  reads(p, "classed:a", true);
  reads(p, "spelled:{a}", true);
  reads(p, "spelled:{a b}", false);
  reads(p, "spelled:a", true);
  reads(p, "@ REM hi\nx", false);
  reads(p, "@ REM\nx", false);
  reads(p, "% REM hi\nx", true);
  reads(p, "% REM\nx", true);
  reads(p, "tie:if", true);
  reads(p, "tie:if ;", true);
}

}  // namespace condition_contract_cases

namespace template_tail_cases {

// A template tail is lexed only where a template continuation can stand.

void run() {
  using condition_contract_cases::reads;
  const cursiv::Parser& p = conformance_test::parser();
  reads(p, "function f { x = 1 }", true);
  reads(p, "function f { x = `a${1}b` }", true);
  reads(p, "function f { x = 1 }\nfunction g { x = `a${1}b` }", true);
  reads(p, "function f { x = 1 }\nfunction g { y = 2; x = `a${z = 1}b` }",
        true);
  reads(p, "function f { x = `a${1}b`; y = 2 }\nfunction g { x = 1 }", true);
  reads(p, "function f { x = 1 }\nfunction g { x = `a${1` }", false);
}

}  // namespace template_tail_cases
