// Left recursion inside or behind optional items: the descent spells out the
// alternatives the items make, as the LR lowering does, so both engines read
// the same trees.
#include <cstdio>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/LeftOptionGrammar.hpp"
#include "leftoptionll/generated/Ll/Module.hpp"
#include "leftoptionlr/generated/Lr/Module.hpp"

namespace left_option_cases {
namespace {

using namespace tlloption::ast;

std::string show(const Node* n) {
  if (const auto* x = cursiv::ast_cast<Name>(n)) return std::string(x->name);
  if (const auto* x = cursiv::ast_cast<Path>(n))
    return x->head == nullptr ? std::string(x->name)
                              : "(" + show(x->head) + "." +
                                    std::string(x->name) + ")";
  if (const auto* x = cursiv::ast_cast<Array>(n))
    return std::string("[") + (x->constant ? "const " : "") + show(x->inner) +
           "]";
  if (const auto* x = cursiv::ast_cast<Marked>(n))
    return "(" + std::string(x->marks.size(), '@') + show(x->inner) + " x)";
  return "?";
}

// The document as one engine reads it: its value, its records and its AST
// spelled so two readings compare.
struct Reading {
  std::string value, records, tree;
  bool text = false;
};

Reading read(const cursiv::Parser& parser, const cursiv::AstReflection& r,
             std::string_view text) {
  Reading out;
  const auto result = parser.parse(text);
  const auto* doc = cursiv::ast_cast<Doc>(result.value);
  if (!result.accepted || !result.diagnostics.empty() || doc == nullptr) {
    out.value = "refused";
    return out;
  }
  out.text = doc->value == nullptr;
  out.value = out.text ? std::string(doc->text) : show(doc->value);
  out.records = cursiv::test::records(result.cst());
  cursiv::test::ast_of(r, *result.value, out.tree);
  cursiv::test::spans_of(r, result.cst(), *result.value, out.tree);
  return out;
}

struct Case {
  std::string_view text, want;
};

// A data rule's value is its text, tokens joined: its records carry the
// nesting.
constexpr Case kCases[] = {
    // The call inside the option.
    {"d: a", "a"},
    {"d: a.b.c", "a.b.c"},
    {"d: .a", "refused"},
    {"q: a", "a"},
    {"q: a.b", "(a.b)"},
    {"q: a.b.c", "((a.b).c)"},
    {"q: a.", "refused"},
    // The call behind the option.
    {"b: y", "y"},
    {"b: y x x", "yxx"},
    {"b: p y x", "pyx"},
    {"b: p y x x", "pyxx"},
    {"b: p p y x x", "ppyxx"},
    {"b: p y", "refused"},
    {"b: p p y x", "refused"},
    // The option writes a flag; an inner reading takes the suffixes it can
    // and leaves the outer one its own.
    {"a: n", "n"},
    {"a: n [] []", "[[n]]"},
    {"a: const n []", "[const n]"},
    {"a: const n [] []", "[const [n]]"},
    {"a: n [] const", "refused"},
    {"a: const const n [] []", "[const [const n]]"},
    // A repetition in front of the call.
    {"m: n x", "(n x)"},
    {"m: @ @ n x", "(@@n x)"},
    {"m: @ n x x", "(@(n x) x)"},
    {"m: n x x", "((n x) x)"},
    {"m: @ @ n", "refused"},
    // Two options in front of the call.
    {"t: z !", "z!"},
    {"t: < z !", "<z!"},
    {"t: > z !", ">z!"},
    {"t: < > z ! !", "<>z!!"},
    {"t: > < z ! !", "><z!!"},
    {"t: < > z", "refused"},
    // Seven options, then a counted repetition, in front of the call.
    {"s: z !", "z!"},
    {"s: 1 3 7 z ! !", "137z!!"},
    {"s: 1 2 3 4 5 6 7 z !", "1234567z!"},
    {"s: 7 1 z !", "refused"},
    {"c: z !", "z!"},
    {"c: + + z !", "++z!"},
    {"c: + z ! !", "+z!!"},
    {"c: + + + z !", "refused"},
    // Eight options in all, one of them in front of the call.
    {"e: z", "z"},
    {"e: z !", "z!"},
    {"e: 1 z !", "1z!"},
    {"e: z 2 4 8 !", "z248!"},
    {"e: 1 z 2 3 4 5 6 7 8 !", "1z2345678!"},
    {"e: 1 z ! !", "1z!!"},
    {"e: 1 z 8 2 !", "refused"},
    {"e: 2 z !", "refused"},
    // An option that is a `tight()` region, in front of the call.
    {"r: z !", "z!"},
    {"r: <- z !", "<-z!"},
    {"r: <- <- z ! !", "<-<-z!!"},
    {"r: #a z #b ! #c", "z!"},
    {"r: #a <- z #b !", "<-z!"},
    {"r: <- z ! #a !", "<-z!!"},
    {"r: < - z !", "refused"},
    {"r: <- z", "refused"},
    {"g: n []", "[n]"},
    {"g: << n []", "[const n]"},
    {"g: << n [] #a []", "[const [n]]"},
    {"g: #a << #b n []", "[const n]"},
    {"g: < < n []", "refused"},
    // The cycle through another rule.
    {"o: o", "o"},
    {"o: o ; ;", "o;;"},
    {"o: ~ o ;", "~o;"},
    {"o: ~ ~ o ; ; ;", "~~o;;;"},
    {"o: ~ o", "refused"},
};

}  // namespace

void run() {
  const auto ll = tlloption::generated::Ll::create_parser();
  const auto lr = tlloption::generated::Lr::create_parser();
  const auto reflection = tlloption::generated::Ll::create_ast_reflection();
  for (const auto& [text, want] : kCases) {
    const Reading a = read(*ll, *reflection, text);
    const Reading b = read(*lr, *reflection, text);
    // The AST compares across engines, and so do a data rule's records,
    // which carry its nesting: the descent lays a turn's own record where LR
    // does not.
    const bool same = a.tree == b.tree && (!a.text || a.records == b.records);
    if (a.value != want || b.value != want || !same)
      std::printf("%.*s: LL %s, LR %s, want %.*s%s\n", int(text.size()),
                  text.data(), a.value.c_str(), b.value.c_str(),
                  int(want.size()), want.data(),
                  same ? "" : " (trees differ)");
    CHECK(a.value == want && b.value == want);
    CHECK(same);
  }
  // Each engine reads the option as the alternatives written out.
  for (const auto* parser : {ll.get(), lr.get()})
    for (const std::string_view text : {"a", "a.b", "a.b.c"}) {
      const Reading option =
          read(*parser, *reflection, "q: " + std::string(text));
      const Reading written =
          read(*parser, *reflection, "w: " + std::string(text));
      CHECK(option.value != "refused" && option.records == written.records &&
            option.tree == written.tree);
    }
}

}  // namespace left_option_cases
