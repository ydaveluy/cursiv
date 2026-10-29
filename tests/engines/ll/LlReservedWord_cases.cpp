#include <cstdio>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ReservedAssertGrammar.hpp"
#include "engines/ll/ReservedEscapeGrammar.hpp"
#include "engines/ll/ReservedWordGrammar.hpp"
#include "escapell/generated/Ll/Module.hpp"

#include "semanticll/generated/Ll/Module.hpp"

namespace ll_reserved_word_cases {

namespace {

std::string names(const cursiv::ParseResult& r) {
  std::string out;
  const auto* d = r.value_as<tllreserved::Doc>();
  if (d == nullptr) return "<none>";
  for (const tllreserved::Item* i : d->items)
    out += std::string(i->guarded ? "?" : "") + std::string(i->name) + ' ';
  return out;
}

std::string reserved(const cursiv::ParseResult& r) {
  std::string out;
  const auto* d = r.value_as<tllreservedassert::Doc>();
  if (d == nullptr) return "<none>";
  for (const tllreservedassert::Item* i : d->items)
    out += std::string(i->keyword ? "?" : "") + std::string(i->name) + ' ';
  return out;
}

// Where the last diagnostic ends.
cursiv::TextOffset reach(const cursiv::ParseResult& r) {
  cursiv::TextOffset far = 0;
  for (const cursiv::ParseDiagnostic& d : r.diagnostics)
    if (d.end > far) far = d.end;
  return far;
}

}  // namespace

// A word a terminal refuses is a word only where no word byte follows it,
// however short a match of that terminal may end.
void run() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  const std::string_view text =
      "reserved:i; if x; iff; if_; whil; while_; w; ifwhile; if i; cas; cases; Loops;";
  const auto r = ll->parse(text);
  CHECK(r.accepted && r.diagnostics.empty());
  CHECK(names(r) == "i ?x iff if_ whil while_ w ifwhile ?i cas cases Loops ");
  for (const std::string_view bad :
       {"if;", "while;", "if while;", "CASE;", "Loop;", "if case;"}) {
    CHECK(!ll->parse("reserved:" + std::string(bad)).diagnostics.empty());
  }
  const auto ruled = ll->parse("reserved:a; ----");
  const auto* doc = ruled.value_as<tllreserved::Doc>();
  CHECK(ruled.accepted && ruled.diagnostics.empty() && doc && doc->ruled);
  CHECK(!ll->parse("reserved:a; ----\n").diagnostics.empty());
  // A repair reads no refused word on into the text behind it: the name
  // after it stays a token of its own.
  const auto repaired = ll->parse("reserved:if if, x;");
  CHECK(!repaired.diagnostics.empty() && reach(repaired) <= 15);
  CHECK(names(repaired).ends_with("?x "));

  // An escape inside a name the word boundary holds: the refused word does
  // not stand before it.
  const auto ell = tllescape::generated::Ll::create_parser();
  const auto escaped =
      ell->parse("in\\u0041; in x; x\\u0041y; \\u0061b; ab:; in ab;");
  CHECK(escaped.accepted && escaped.diagnostics.empty());
  const auto* d = escaped.value_as<tllescape::Doc>();
  std::string got;
  if (d != nullptr)
    for (const tllescape::Item* i : d->items)
      got += std::string(i->guarded ? "?" : "") + std::string(i->name) +
             std::string(i->label) + ' ';
  CHECK(got == "in\\u0041 ?x x\\u0041y \\u0061b ab: ?ab ");
  for (const std::string_view bad : {"in;", "in:;", "in\\u00;"})
    CHECK(!ell->parse(bad).diagnostics.empty());
  // A name that asserts what follows it is read behind a relay, and the words
  // a keyword spells are no more its than a plain name's.
  const auto result =
      ll->parse("assert:if; x; iff; if_; ifx; while; whilex; i;");
  CHECK(result.accepted && result.diagnostics.empty());
  CHECK(reserved(result) == "? x iff if_ ifx ? whilex i ");
}

}  // namespace ll_reserved_word_cases
