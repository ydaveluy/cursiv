#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "indirectfoldll/generated/Ll/Module.hpp"

#include "semanticll/generated/Ll/Module.hpp"

namespace {

template <class Parser, class Reflection>
std::string read(Parser& p, Reflection& r, std::string_view prefix, std::string_view text) {
  const std::string input = std::string(prefix) + std::string(text);
  const auto result = p->parse(input);
  CHECK(result.accepted && result.diagnostics.empty());
  if (!result.value) return "";
  std::string out;
  cursiv::test::ast_of(*r, *result.value, out);
  return out;
}

// A rejected call gives its consumed prefix back without attaching a partial
// node. The caller and the text after it remain in the tree.
void abandoned(const cursiv::Parser& parser,
               const cursiv::AstReflection& reflection, std::string_view text,
               std::string_view want,
               std::initializer_list<std::pair<unsigned, unsigned>> spans,
               std::string_view prefix) {
  const std::string input = std::string(prefix) + std::string(text);
  const auto result = parser.parse(input);
  CHECK(result.accepted && result.value != nullptr && result.cst().recovered());
  std::string out;
  if (result.value) cursiv::test::ast_of(reflection, *result.value, out);
  bool right = result.diagnostics.size() == spans.size();
  auto expected = spans.begin();
  for (const auto& diagnostic : result.diagnostics) {
    if (expected == spans.end()) break;
    right = right && diagnostic.kind == cursiv::DiagnosticKind::incomplete &&
            diagnostic.begin == prefix.size() + expected->first &&
            diagnostic.end == prefix.size() + expected->second &&
            parser.getGrammar().terminalName(diagnostic.element) == "ID" &&
            diagnostic.also.empty();
    ++expected;
  }
  if (out != want || !right) {
    std::cerr << "abandon text: " << text << "\nwant: " << want
              << "\ntree: " << out << '\n';
    for (const auto& diagnostic : result.diagnostics)
      std::cerr << "  kind=" << static_cast<unsigned>(diagnostic.kind)
                << " span=" << diagnostic.begin << "-" << diagnostic.end
                << " id=" << diagnostic.element << " terminal="
                << parser.getGrammar().terminalName(diagnostic.element) << '\n';
  }
  CHECK(out == want);
  CHECK(right);
}

// Every retained text has an independent expected tree.
template <class Parser, class Reflection>
void expect_tree(std::string_view prefix, Parser& parser, Reflection& reflection,
           std::initializer_list<std::pair<std::string_view, std::string_view>> cases) {
  for (const auto& [text, want] : cases) {
    const auto got = read(parser, reflection, prefix, text);
    if (got != want)
      std::cerr << "text: " << text << "\nwant: " << want << "\nLL: " << got << '\n';
    CHECK(got == want);
  }
}

}  // namespace

int main() {
  const auto parser = tllsemantic::generated::Ll::create_parser();
  const auto reflection = tllsemantic::generated::Ll::create_ast_reflection();

  // A paragraph's lines go on before the document's loop opens the next
  // paragraph, whatever block stands before; with no `;` last, the document's
  // loop turns first.

  // One `*` both an operator and a glob's suffix: an operand is a call of its
  // own, whose turns win over stopping.
  namespace fold = tllindirect::generated;
  auto foldParser = fold::Ll::create_parser();
  auto foldReflection = fold::Ll::create_ast_reflection();
  expect_tree("", foldParser, foldReflection,
        {{"1*2*3",
          "Document{values0=Binary{left=Number{} right=Glob{value=Number{} } } "
          "values1=Number{} }"},
         {"1*2*",
          "Document{values0=Binary{left=Number{} "
          "right=Glob{value=Number{} } } }"},
         {"*1*2*",
          "Document{values0=Glob{value=Binary{left=Number{} "
          "right=Glob{value=Number{} } } } }"},
         {"**1", "Document{values0=Glob{value=Glob{value=Number{} } } }"},
         {"1*2**",
          "Document{values0=Binary{left=Number{} "
          "right=Glob{value=Glob{value=Number{} } } } }"},
         {"1*2*3*",
          "Document{values0=Binary{left=Number{} "
          "right=Glob{value=Binary{left=Number{} right=Number{} } } } }"},
         {"1*2*3*4",
          "Document{values0=Binary{left=Number{} "
          "right=Glob{value=Binary{left=Number{} right=Number{} } } } "
          "values1=Number{} }"}});
  // A keyword its word terminal also spells, whether the readings part at it
  // or at the condition above it.
  constexpr std::string_view implies =
      "Doc{items0=Both{left=Name{} right=Name{} } }";
  expect_tree("order:keyword:", parser, reflection,
        {{"a and b", implies},
         {"a implies b", implies},
         {"a implies b c",
          "Doc{items0=Both{left=Name{} right=Name{} } items1=Name{} }"},
         {"a and b ?",
          "Doc{items0=Mark{value=Both{left=Name{} right=Name{} } } }"},
         {"a and b implies c",
          "Doc{items0=Both{left=Both{left=Name{} right=Name{} } "
          "right=Name{} } }"},
         {"! a implies b",
          "Doc{items0=Box{value=Both{left=Name{} right=Name{} } } }"}});
  // The loop that goes on at a definition decides before the guards of the
  // definition do; with no definition to open, the guard goes on.

  // A ladder closed by a left recursion: the arms of a rung, written either
  // way round, are chosen where the rung starts, and the suffix goes on in the
  // operand it ends before it climbs out.
  constexpr std::string_view compared =
      "Doc{items0=Compare{left=Mark{value=Name{} } right=Name{} } }";

  constexpr std::string_view summed =
      "Doc{items0=Add{left=Mark{value=Name{} } right=Compare{left=Name{} "
      "right=Name{} } } }";
  constexpr std::string_view matched =
      "Doc{items0=Match{left=Name{} right=Match{left=Name{} right=Name{} } } }";
  // The suffix again behind a rung that closed: the box is its left.
  constexpr std::string_view again =
      "Doc{items0=Match{left=Box{value=Match{left=Name{} right=Name{} } } "
      "right=Name{} } }";
  constexpr std::string_view behind =
      "Doc{items0=Match{left=Box{value=Name{} } right=Name{} } }";
  constexpr std::string_view tight =
      "Doc{items0=Add{left=Name{} right=Match{left=Name{} right=Name{} } } }";
  // A wrapper again over the suffix's result, which no operand could take.
  constexpr std::string_view boxed =
      "Doc{items0=Box{value=Match{left=Box{value=Name{} } "
      "right=Box{value=Name{} } } } }";
  expect_tree("order:cycle:", parser, reflection,
        {{"min a from b", compared},
         {"min a + b from c", summed},
         {"a matches b matches c", matched},
         {"a matches b ago matches c", again},
         {"a ago matches c", behind},
         {"a + b matches c", tight},
         {"a from b matches c",
          "Doc{items0=Compare{left=Name{} right=Match{left=Name{} "
          "right=Name{} } } }"},
         {"min a from b + c",
          "Doc{items0=Add{left=Compare{left=Mark{value=Name{} } "
          "right=Name{} } right=Name{} } }"},
         {"trim min a from b",
          "Doc{items0=Box{value=Compare{left=Mark{value=Name{} } "
          "right=Name{} } } }"},
         {"a + b from c matches d",
          "Doc{items0=Add{left=Name{} right=Compare{left=Name{} "
          "right=Match{left=Name{} right=Name{} } } } }"},
         {"a from b matches c matches d",
          "Doc{items0=Compare{left=Name{} right=Match{left=Name{} "
          "right=Match{left=Name{} right=Name{} } } } }"},
         {"a + b + c matches d",
          "Doc{items0=Add{left=Name{} right=Match{left=Add{left=Name{} "
          "right=Name{} } right=Name{} } } }"},
         {"a + b matches c matches d",
          "Doc{items0=Add{left=Name{} right=Match{left=Name{} "
          "right=Match{left=Name{} right=Name{} } } } }"},
         {"a from b + c matches d",
          "Doc{items0=Compare{left=Name{} right=Match{left=Add{left=Name{} "
          "right=Name{} } right=Name{} } } }"},
         {"a from trim b matches c",
          "Doc{items0=Compare{left=Name{} right=Match{left=Box{value=Name{} } "
          "right=Name{} } } }"},
         {"a ago matches b ago ago", boxed}});

  // The wrapping arm written after the bare one in the rungs the name gives.

  expect_tree("order:last:", parser, reflection,
      {{"min a from a from a matches a",
        "Doc{items0=Compare{left=Mark{value=Name{} } "
        "right=Match{left=Compare{left=Name{} right=Name{} } right=Name{} } } "
        "}"},
       {"a + a from a + a matches a",
        "Doc{items0=Add{left=Name{} right=Compare{left=Name{} "
        "right=Match{left=Add{left=Name{} right=Name{} } right=Name{} "
        "} } } }"},
       {"a from a from a matches a matches a",
        "Doc{items0=Compare{left=Name{} right=Match{left=Compare{left=Name{} "
        "right=Match{left=Name{} right=Name{} } } right=Name{} } } }"},
       {"a from trim a matches a matches a",
        "Doc{items0=Compare{left=Name{} "
        "right=Match{left=Box{value=Match{left=Name{} right=Name{} } } "
        "right=Name{} } } }"},
       {"a from trim trim a matches a matches a",
        "Doc{items0=Compare{left=Name{} "
        "right=Match{left=Box{value=Box{value=Match{left=Name{} right=Name{} } "
        "} } right=Name{} } } }"}});
  // A keyword two rules spell alike: the rule written first reads it.

  // A declaration whose type name refuses a reserved word against an
  // expression that shifts the same name: the one written first reads it.
  constexpr std::string_view declared =
      "Doc{items0=Decl{type=Name{} name=Name{} } }";
  constexpr std::string_view product =
      "Doc{items0=Both{left=Name{} right=Name{} } }";
  expect_tree("order:first:", parser, reflection,
      {{"a * b ;", declared},
       {"const a * b ;", declared},
       {"a * if ;", declared},
       {"if * b ;", product},
       {"a * b ; if * c ;",
        "Doc{items0=Decl{type=Name{} name=Name{} } "
        "items1=Both{left=Name{} right=Name{} } }"}});
  expect_tree("order:refusal:", parser, reflection,
        {{"a * b ;", product},
         {"const a * b ;", declared},
         {"a * if ;", product},
         {"if * b ;", product},
         {"a * b ; if * c ;",
          "Doc{items0=Both{left=Name{} right=Name{} } "
          "items1=Both{left=Name{} right=Name{} } }"}});
  // Giving up a declaration leaves its condition's name to the expression
  // arm, which may read it. The caller's earlier and later items survive, and
  // repeated abandons rearm the loop instead of leaking an unwind into it.
  for (const bool first : {true, false}) {
    const auto pair =
        [&](std::string_view text, std::string_view want,
            std::initializer_list<std::pair<unsigned, unsigned>> spans) {
          abandoned(*parser, *reflection, text, want, spans,
                    first ? "order:first:" : "order:refusal:");
        };
    const std::string_view after =
        first ? "Doc{items0=Both{left=Name{} right=Name{} } "
                "items1=Decl{type=Name{} name=Name{} } }"
              : "Doc{items0=Both{left=Name{} right=Name{} } "
                "items1=Both{left=Name{} right=Name{} } }";
    pair("const if * b ; a * c ;", after, {{0, 5}});
    pair("const const if * b ; a * c ;", after, {{0, 11}});
    pair("const if * b ;", product, {{0, 5}});
    pair("a * c ; const if * b ; a * c ;",
         first ? "Doc{items0=Decl{type=Name{} name=Name{} } "
                 "items1=Both{left=Name{} right=Name{} } "
                 "items2=Decl{type=Name{} name=Name{} } }"
               : "Doc{items0=Both{left=Name{} right=Name{} } "
                 "items1=Both{left=Name{} right=Name{} } "
                 "items2=Both{left=Name{} right=Name{} } }",
         {{8, 13}});
    pair("const if * b ; const if * c ; a * d ;",
         first ? "Doc{items0=Both{left=Name{} right=Name{} } "
                 "items1=Both{left=Name{} right=Name{} } "
                 "items2=Decl{type=Name{} name=Name{} } }"
               : "Doc{items0=Both{left=Name{} right=Name{} } "
                 "items1=Both{left=Name{} right=Name{} } "
                 "items2=Both{left=Name{} right=Name{} } }",
         {{0, 5}, {15, 20}});
  }
  // The arm with a tail wins whenever it can read, whatever the loop of the
  // child before it takes.
  constexpr std::string_view tailed =
      "Doc{items0=Both{left=Mark{value=Title{} } right=Title{} } }";
  expect_tree("order:whole:", parser, reflection,
        {{"x", "Doc{items0=Box{value=Mark{value=Title{} } } }"},
         {"x x", tailed},
         {"x x x", tailed},
         {"x x x x", tailed}});
  return cursiv::test::report("LlTextOrder_test");
}
